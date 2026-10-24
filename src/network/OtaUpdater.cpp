#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "CrossPointSettings.h"
#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
constexpr char stableReleaseUrl[] = "https://api.github.com/repos/Chiendroid/crossB-OS/releases/latest";
constexpr char devReleaseUrl[] = "https://api.github.com/repos/Chiendroid/crossB-OS/releases?per_page=1";
}  // namespace

std::string OtaUpdater::sanitizeChangelog(const char* raw) {
  if (!raw || *raw == '\0') return "";

  std::string result;
  result.reserve(strlen(raw));

  const auto* p = reinterpret_cast<const unsigned char*>(raw);
  bool atLineStart = true;
  bool lastWasNewline = false;

  while (*p) {
    if (*p == '\r') {
      p++;
      continue;
    }
    if (*p == '\n') {
      if (!lastWasNewline && !result.empty()) {
        result.push_back('\n');
        lastWasNewline = true;
      }
      atLineStart = true;
      p++;
      continue;
    }

    // Skip markdown headings and leading spaces at line start
    if (atLineStart) {
      while (*p == '#' || *p == ' ' || *p == '\t') {
        p++;
      }
      if (*p == '\0' || *p == '\n' || *p == '\r') {
        continue;
      }
      atLineStart = false;
      lastWasNewline = false;
    }

    // Strip markdown formatting symbols
    if (*p == '*' || *p == '_') {
      p++;
      continue;
    }

    // Convert markdown link [title](url) into title
    if (*p == '[') {
      p++;
      continue;
    }
    if (*p == ']') {
      p++;
      if (*p == '(') {
        p++;
        while (*p && *p != ')' && *p != '\n') {
          p++;
        }
        if (*p == ')') p++;
      }
      continue;
    }

    unsigned char c = *p;
    if (c < 0x80) {
      if (c >= 32 || c == '\t') {
        result.push_back(static_cast<char>(c));
        lastWasNewline = false;
      }
      p++;
    } else if ((c & 0xE0) == 0xC0) {
      // 2-byte UTF-8
      if (*(p + 1) && (*(p + 1) & 0xC0) == 0x80) {
        result.push_back(static_cast<char>(*p));
        result.push_back(static_cast<char>(*(p + 1)));
        p += 2;
        lastWasNewline = false;
      } else {
        p++;
      }
    } else if ((c & 0xF0) == 0xE0) {
      // 3-byte UTF-8
      if (*(p + 1) && *(p + 2) && (*(p + 1) & 0xC0) == 0x80 && (*(p + 2) & 0xC0) == 0x80) {
        if (c == 0xE2) {
          // Replace en-dash or em-dash with standard ASCII '-'
          if (*(p + 1) == 0x80 && (*(p + 2) == 0x93 || *(p + 2) == 0x94)) {
            result.push_back('-');
            lastWasNewline = false;
          }
          // Skip other 3-byte symbols and dingbats
          p += 3;
        } else {
          result.push_back(static_cast<char>(*p));
          result.push_back(static_cast<char>(*(p + 1)));
          result.push_back(static_cast<char>(*(p + 2)));
          p += 3;
          lastWasNewline = false;
        }
      } else {
        p++;
      }
    } else if ((c & 0xF8) == 0xF0) {
      // 4-byte UTF-8 (Emoji) - strip completely
      size_t skip = 1;
      while (skip < 4 && *(p + skip) && (*(p + skip) & 0xC0) == 0x80) {
        skip++;
      }
      p += skip;
    } else {
      p++;
    }
  }

  return result;
}

const char* OtaUpdater::getDefaultCurrentChangelog() {
  return "- Nang cap he thong len phien ban 0.2.0-dev\n"
         "- Toi uu trinh duyet web, giam thieu loi bo nho OOM\n"
         "- Sua loi crash khi chon co chu trong trinh doc Epub\n"
         "- Sua loi man hinh ngu khong hien thi bia sach\n"
         "- Cai tien giao dien cap nhat OTA va nhat ky thay doi";
}

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  const char* targetUrl = (SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV) ? devReleaseUrl : stableReleaseUrl;
  LOG_DBG("OTA", "Checking for update [Channel: %s] from %s (current: %s)",
          SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV ? "Dev" : "Release", targetUrl, CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  // Heap-allocated: the parser embeds a 2 KB JSON token buffer.
  auto releaseParserPtr = makeUniqueNoThrow<ReleaseJsonParser>();
  if (!releaseParserPtr) {
    LOG_ERR("OTA", "OOM: release parser");
    return OOM_ERROR;
  }
  ReleaseJsonParser& releaseParser = *releaseParserPtr;
  releaseParser.setFirmwareAssetName("");
  // Each board updates from crosspoint-<version>-<device>.bin. The combined
  // C3 image uses x3-x4; other asset suffixes match their firmware board tag.
  const bool isX4 = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char assetSuffix[20] = "-x3-x4";
  if (!isX4) {
    snprintf(assetSuffix, sizeof(assetSuffix), "-%.*s", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  }
  char assetName[48] = {};
  bool assetNameSet = false;
  const bool ok = HttpDownloader::fetchUrl(targetUrl, [&](const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (!assetNameSet && offset < len) {
      releaseParser.feed(reinterpret_cast<const char*>(data + offset), 1);
      offset++;
      if (releaseParser.foundTag()) {
        snprintf(assetName, sizeof(assetName), "crossB-OS-%s%s.bin", releaseParser.getTagName(), assetSuffix);
        releaseParser.setFirmwareAssetName(assetName);
        assetNameSet = true;
      }
    }
    if (offset < len) releaseParser.feed(reinterpret_cast<const char*>(data + offset), len - offset);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  const char* body = releaseParser.getReleaseBody();
  if (body && *body) {
    releaseNotes = sanitizeChangelog(body);
  }
  if (releaseNotes.empty()) {
    releaseNotes = getDefaultCurrentChangelog();
  }
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu notes_len=%zu", latestVersion.c_str(), otaSize, releaseNotes.size());
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

namespace {
const char* skipToVersionDigits(const char* s) {
  if (!s) return "";
  while (*s && !isdigit(static_cast<unsigned char>(*s))) {
    s++;
  }
  return s;
}
}  // namespace

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty()) {
    return false;
  }

  // Tự động tìm vị trí chữ số đầu tiên trong chuỗi phiên bản (bỏ qua 'v', 'crossB-OS-', v.v.)
  const char* lStr = skipToVersionDigits(latestVersion.c_str());
  const char* cStr = skipToVersionDigits(CROSSPOINT_VERSION);

  // Nếu 2 chuỗi phiên bản chuẩn hoá trùng khớp hoàn toàn (ví dụ đều là "0.1.2-dev" hoặc "0.1.2"):
  if (strcmp(lStr, cStr) == 0) {
    return false;
  }

  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;

  // semantic version check (only match on 3 segments)
  sscanf(lStr, "%d.%d.%d", &latestMajor, &latestMinor, &latestPatch);
  sscanf(cStr, "%d.%d.%d", &currentMajor, &currentMinor, &currentPatch);

  if (latestMajor != currentMajor) return latestMajor > currentMajor;
  if (latestMinor != currentMinor) return latestMinor > currentMinor;
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // Nếu các con số major.minor.patch bằng nhau:
  // Nếu bản hiện tại là pre-release (-dev, -alpha, -beta, -rc) và bản mới là bản release chính thức (không có dấu '-'):
  const bool currentIsPre = (strchr(cStr, '-') != nullptr);
  const bool latestIsPre = (strchr(lStr, '-') != nullptr);

  if (currentIsPre && !latestIsPre) {
    return true;  // Nâng cấp từ dev/alpha lên bản release chính thức
  }

  // Nếu ở kênh Dev và có hậu tố khác nhau:
  if (SETTINGS.otaChannel == CrossPointSettings::OTA_CHANNEL_DEV) {
    return strcmp(lStr, cStr) > 0;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (otaUrl.empty()) {
    LOG_ERR("OTA", "Firmware URL is empty");
    return HTTP_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  // All S3 boards share a chip_id, so also scan the stream for the embedded
  // board tag (FirmwareBoardTag.h). An untagged image passes; a tag naming a
  // different board aborts the download. The wrong image may partially land in
  // the inactive OTA slot, but esp_ota_abort() below means it never becomes
  // the boot target.
  board_tag::Scanner tagScanner;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;  // abort the transfer
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
