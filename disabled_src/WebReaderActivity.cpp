#include "WebReaderActivity.h"

#include <Arduino.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <Utf8.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/icons/browserIcons.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

namespace {

constexpr int MAX_ULTRA_LIGHT_IMAGES = 2;
constexpr int TARGET_IMG_W = 432;
constexpr int TARGET_IMG_H = 172;
constexpr int LOGO_SIZE = 36;
constexpr const char* BOOKMARKS_FILE_PATH = "/.crosspoint/web_bookmarks.json";
constexpr const char* LEGACY_BOOKMARKS_FILE_PATH = "/web_bookmarks.txt";

void appendUtf8Codepoint(std::string& out, int code) {
  if (code <= 0) return;
  if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x110000) {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

std::string urlEncode(const std::string& value) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (const char c : value) {
    if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
    } else if (c == ' ') {
      escaped << "%20";
    } else {
      escaped << '%' << std::uppercase << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
    }
  }
  return escaped.str();
}

void renderBionicText(GfxRenderer& renderer, int fontId, int startX, int startY, const std::string& line) {
  int curX = startX;
  size_t i = 0;
  const size_t len = line.size();
  while (i < len) {
    if (line[i] == ' ') {
      curX += renderer.getTextWidth(fontId, " ");
      ++i;
      continue;
    }
    size_t wordStart = i;
    while (i < len && line[i] != ' ') {
      ++i;
    }
    size_t wordLen = i - wordStart;
    size_t boldLen = (wordLen <= 3) ? 1 : ((wordLen * 45) / 100 + 1);
    if (boldLen > wordLen) boldLen = wordLen;

    std::string boldPart = line.substr(wordStart, boldLen);
    std::string restPart = line.substr(wordStart + boldLen, wordLen - boldLen);

    renderer.drawText(fontId, curX, startY, boldPart.c_str(), true, EpdFontFamily::BOLD);
    curX += renderer.getTextWidth(fontId, boldPart.c_str(), EpdFontFamily::BOLD);

    if (!restPart.empty()) {
      renderer.drawText(fontId, curX, startY, restPart.c_str(), true, EpdFontFamily::REGULAR);
      curX += renderer.getTextWidth(fontId, restPart.c_str(), EpdFontFamily::REGULAR);
    }
  }
}

std::string urlDecodeParam(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out += ' ';
    } else if (value[i] == '%' && i + 2 < value.size()) {
      auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const int hi = hexVal(value[i + 1]);
      const int lo = hexVal(value[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out += static_cast<char>((hi << 4) | lo);
        i += 2;
        continue;
      }
      out += value[i];
    } else {
      out += value[i];
    }
  }
  return out;
}

std::string unwrapSearchRedirectUrl(const std::string& url) {
  for (const char* key : {"uddg=", "/url?q=", "&q=http", "?url=http", "&url=http"}) {
    const size_t pos = url.find(key);
    if (pos != std::string::npos) {
      size_t valStart = pos + std::strlen(key);
      if (std::strstr(key, "http") != nullptr) {
        valStart -= 4;
      }
      const size_t amp = url.find('&', valStart);
      std::string decoded = urlDecodeParam(url.substr(valStart, amp == std::string::npos ? std::string::npos : amp - valStart));
      if (decoded.rfind("http://", 0) == 0 || decoded.rfind("https://", 0) == 0) {
        return decoded;
      }
    }
  }
  return url;
}

std::string extractDomainFromUrl(const std::string& url) {
  std::string clean = url;
  if (clean.rfind("https://", 0) == 0) clean = clean.substr(8);
  else if (clean.rfind("http://", 0) == 0) clean = clean.substr(7);
  if (clean.rfind("www.", 0) == 0) clean = clean.substr(4);
  const size_t slash = clean.find('/');
  if (slash != std::string::npos) clean = clean.substr(0, slash);
  const size_t qmark = clean.find('?');
  if (qmark != std::string::npos) clean = clean.substr(0, qmark);
  return clean;
}

std::string sanitizeDomainForFile(const std::string& domain) {
  std::string out;
  for (const char c : domain) {
    if (isalnum(static_cast<unsigned char>(c))) {
      out += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    } else {
      out += '_';
    }
  }
  if (out.size() > 24) out = out.substr(0, 24);
  return out.empty() ? "site" : out;
}

int detectBuiltinLogoIndex(const std::string& domain) {
  if (domain.find("vnexpress.") != std::string::npos) return 0;
  if (domain.find("wikipedia.") != std::string::npos) return 1;
  if (domain.find("tuoitre.") != std::string::npos) return 2;
  if (domain.find("dantri.") != std::string::npos) return 3;
  if (domain.find("ycombinator.") != std::string::npos) return 4;
  if (domain.find("npr.org") != std::string::npos) return 5;
  return -1;
}

std::string makeFriendlySiteName(const std::string& pageTitle, const std::string& domain) {
  if (!pageTitle.empty()) {
    std::string cleanTitle = pageTitle;
    const size_t dash = cleanTitle.find(" - ");
    if (dash != std::string::npos && dash >= 3) cleanTitle = cleanTitle.substr(0, dash);
    const size_t pipe = cleanTitle.find(" | ");
    if (pipe != std::string::npos && pipe >= 3) cleanTitle = cleanTitle.substr(0, pipe);
    if (cleanTitle.size() > 16) cleanTitle = cleanTitle.substr(0, 15) + "..";
    return cleanTitle;
  }
  std::string name = domain;
  const size_t dot = name.find('.');
  if (dot != std::string::npos && dot > 0) {
    name = name.substr(0, dot);
  }
  if (!name.empty()) {
    name[0] = static_cast<char>(toupper(static_cast<unsigned char>(name[0])));
  }
  return name.empty() ? "Trang Web" : name;
}

std::string decodeHtmlEntities(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '&') {
      const size_t semi = input.find(';', i);
      if (semi != std::string::npos && semi - i <= 10) {
        std::string entity = input.substr(i + 1, semi - i - 1);
        if (entity == "amp") { out += '&'; i = semi; continue; }
        if (entity == "lt") { out += '<'; i = semi; continue; }
        if (entity == "gt") { out += '>'; i = semi; continue; }
        if (entity == "quot" || entity == "ldquo" || entity == "rdquo") { out += '"'; i = semi; continue; }
        if (entity == "apos" || entity == "lsquo" || entity == "rsquo" || entity == "#39" || entity == "#x27" || entity == "#X27") { out += '\''; i = semi; continue; }
        if (entity == "nbsp") { out += ' '; i = semi; continue; }
        if (entity == "ndash" || entity == "mdash") { out += '-'; i = semi; continue; }
        if (entity == "bull" || entity == "middot") { out += "*"; i = semi; continue; }
        if (entity.size() > 1 && entity[0] == '#') {
          long code = 0;
          char* endp = nullptr;
          if (entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X')) {
            code = strtol(entity.c_str() + 2, &endp, 16);
          } else {
            code = strtol(entity.c_str() + 1, &endp, 10);
          }
          if (endp && endp != entity.c_str() + 1 && endp != entity.c_str() + 2) {
            if (code == 160) {
              out += ' ';
              i = semi;
              continue;
            }
            if (code > 0 && code < 0x110000) {
              appendUtf8Codepoint(out, static_cast<uint32_t>(code));
              i = semi;
              continue;
            }
          }
        }
      }
    }
    if (i + 1 < input.size() && static_cast<unsigned char>(input[i]) == 0xC2 &&
        static_cast<unsigned char>(input[i + 1]) == 0xA0) {
      out += ' ';
      ++i;
      continue;
    }
    out += input[i];
  }
  return out;
}

std::string extractAttr(const std::string& tag, const std::string& attrName) {
  std::string tagLower = tag;
  std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);
  std::string key = attrName + "=";
  size_t pos = tagLower.find(key);
  while (pos != std::string::npos) {
    if (pos == 0 || isspace(static_cast<unsigned char>(tagLower[pos - 1])) || tagLower[pos - 1] == '"' || tagLower[pos - 1] == '\'') {
      break;
    }
    pos = tagLower.find(key, pos + 1);
  }
  if (pos == std::string::npos) return "";
  size_t valStart = pos + key.size();
  while (valStart < tag.size() && isspace(static_cast<unsigned char>(tag[valStart]))) valStart++;
  if (valStart >= tag.size()) return "";
  char quote = tag[valStart];
  if (quote == '"' || quote == '\'') {
    size_t valEnd = tag.find(quote, valStart + 1);
    if (valEnd != std::string::npos) {
      return decodeHtmlEntities(tag.substr(valStart + 1, valEnd - valStart - 1));
    }
  } else {
    size_t valEnd = tag.find_first_of(" \t\r\n>", valStart);
    return decodeHtmlEntities(tag.substr(valStart, valEnd == std::string::npos ? std::string::npos : valEnd - valStart));
  }
  return "";
}

bool isDecorativeOrTinyImageUrl(const std::string& url) {
  if (url.empty()) return true;
  std::string lower = url;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.rfind("data:", 0) == 0) return true;
  if (lower.find(".svg") != std::string::npos || lower.find(".gif") != std::string::npos ||
      lower.find(".ico") != std::string::npos) {
    return true;
  }
  if (lower.find("pixel") != std::string::npos || lower.find("1x1") != std::string::npos ||
      lower.find("blank") != std::string::npos || lower.find("spacer") != std::string::npos ||
      lower.find("avatar") != std::string::npos || lower.find("favicon") != std::string::npos ||
      lower.find("icon") != std::string::npos || lower.find("logo") != std::string::npos ||
      lower.find("doubleclick") != std::string::npos || lower.find("analytics") != std::string::npos ||
      lower.find("external-content.duckduckgo.com") != std::string::npos ||
      lower.find("th.bing.com") != std::string::npos) {
    return true;
  }
  return false;
}

std::string shortenUrlForBar(const std::string& url) {
  if (url.empty()) return "";
  std::string clean = url;
  if (clean.rfind("https://", 0) == 0) clean = clean.substr(8);
  else if (clean.rfind("http://", 0) == 0) clean = clean.substr(7);
  if (clean.rfind("www.", 0) == 0) clean = clean.substr(4);
  if (!clean.empty() && clean.back() == '/') clean.pop_back();
  if (clean.size() > 36) {
    clean = clean.substr(0, 34) + "..";
  }
  return clean;
}

std::string fitTextToWidth(GfxRenderer& renderer, int fontId, const std::string& text, int maxW,
                           EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (renderer.getTextWidth(fontId, text.c_str(), style) <= maxW) return text;
  std::string out = text;
  while (out.size() > 4 && renderer.getTextWidth(fontId, (out + "..").c_str(), style) > maxW) {
    out.pop_back();
  }
  return out + "..";
}

void wrapTextToLines(GfxRenderer& renderer, int fontId, const std::string& text, int maxW,
                     std::vector<std::string>& outLines) {
  std::istringstream stream(text);
  std::string word;
  std::string line;
  while (stream >> word) {
    const std::string testLine = line.empty() ? word : (line + " " + word);
    if (renderer.getTextWidth(fontId, testLine.c_str()) <= maxW) {
      line = testLine;
    } else {
      if (!line.empty()) outLines.push_back(line);
      line = word;
    }
  }
  if (!line.empty()) outLines.push_back(line);
}

void drawCenteredBoxText(GfxRenderer& renderer, int fontId, int boxX, int boxY, int boxW, int yOffset,
                         const char* text, bool black = true, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int tw = renderer.getTextWidth(fontId, text, style);
  const int tx = boxX + std::max(2, (boxW - tw) / 2);
  renderer.drawText(fontId, tx, boxY + yOffset, text, black, style);
}

void drawModernButton(GfxRenderer& renderer, int x, int y, int w, int h, int radius, bool active = false, int borderWidth = 1) {
  if (active) {
    renderer.fillRoundedRect(x, y, w, h, radius, Color::Black);
  } else {
    renderer.fillRoundedRect(x, y, w, h, radius, Color::White);
    renderer.drawRoundedRect(x, y, w, h, borderWidth, radius, true);
  }
}

void drawLucideIcon(GfxRenderer& renderer, const freeink::Icon& icon, int x, int y, bool inverted = false) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    for (int col = 0; col < icon.w; ++col) {
      const uint8_t byte = icon.bits[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + col, y + row, !inverted);
      }
    }
  }
}

}  // namespace

WebReaderActivity::WebReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialUrl)
    : Activity("WebReader", renderer, mappedInput), currentUrl(std::move(initialUrl)) {
  initBookmarks();
}

void WebReaderActivity::initBookmarks() {
  bookmarks = {
      {"VnExpress", "https://vnexpress.net", "vnexpress.net", "Tin tức", 0, ""},
      {"Wikipedia VI", "https://vi.wikipedia.org/wiki/Trang_Ch%C3%ADnh?useparsoid=0", "vi.wikipedia.org", "Bách khoa", 1, ""},
      {"Tuổi Trẻ", "https://tuoitre.vn", "tuoitre.vn", "Thời sự", 2, ""},
      {"Dân Trí", "https://dantri.com.vn", "dantri.com.vn", "Xã hội", 3, ""},
      {"Hacker News", "https://news.ycombinator.com", "news.ycombinator.com", "Công nghệ", 4, ""},
      {"NPR Text", "https://text.npr.org", "text.npr.org", "Đọc nhanh", 5, ""},
  };
  loadSavedBookmarks();
}

void WebReaderActivity::loadSavedBookmarks() {
  const char* loadPath = BOOKMARKS_FILE_PATH;
  bool isLegacy = false;
  if (!Storage.exists(BOOKMARKS_FILE_PATH) && Storage.exists(LEGACY_BOOKMARKS_FILE_PATH)) {
    loadPath = LEGACY_BOOKMARKS_FILE_PATH;
    isLegacy = true;
  } else if (!Storage.exists(BOOKMARKS_FILE_PATH)) {
    return;
  }
  HalFile f;
  if (!Storage.openFileForRead("WEB", loadPath, f)) return;

  std::string content;
  char buf[256];
  while (true) {
    int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf));
    if (n <= 0) break;
    content.append(buf, static_cast<size_t>(n));
  }
  f.close();

  std::vector<Bookmark> loaded;
  std::istringstream ss(content);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty()) continue;
    std::vector<std::string> parts;
    std::istringstream ls(line);
    std::string part;
    while (std::getline(ls, part, '\t')) {
      parts.push_back(part);
    }
    if (parts.size() >= 5) {
      Bookmark bm;
      bm.name = parts[0];
      bm.url = parts[1];
      bm.subtitle = parts[2];
      bm.tag = parts[3];
      char* endp = nullptr;
      bm.logoIndex = static_cast<int>(strtol(parts[4].c_str(), &endp, 10));
      if (endp == parts[4].c_str()) {
        bm.logoIndex = -1;
      }
      bm.customLogoPath = (parts.size() >= 6) ? parts[5] : "";
      loaded.push_back(bm);
    }
  }
  if (!loaded.empty()) {
    bookmarks = std::move(loaded);
  }
  if (isLegacy) {
    saveBookmarksToStorage();
    Storage.remove(LEGACY_BOOKMARKS_FILE_PATH);
  }
}

void WebReaderActivity::saveBookmarksToStorage() const {
  HalFile f;
  if (!Storage.openFileForWrite("WEB", BOOKMARKS_FILE_PATH, f)) return;
  std::ostringstream ss;
  for (const auto& bm : bookmarks) {
    ss << bm.name << '\t' << bm.url << '\t' << bm.subtitle << '\t' << bm.tag << '\t'
       << bm.logoIndex << '\t' << bm.customLogoPath << '\n';
  }
  const std::string data = ss.str();
  f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.close();
}

bool WebReaderActivity::isCurrentPageBookmarked() const {
  if (currentUrl.empty()) return false;
  const std::string curDomain = extractDomainFromUrl(currentUrl);
  for (const auto& bm : bookmarks) {
    if (bm.url == currentUrl || (!curDomain.empty() && extractDomainFromUrl(bm.url) == curDomain)) {
      return true;
    }
  }
  return false;
}

bool WebReaderActivity::downloadWebsiteLogo(const std::string& siteUrl, std::string& outLogoPath) {
  const std::string domain = extractDomainFromUrl(siteUrl);
  if (domain.empty()) return false;

  const std::string logoPath = "/web_logo_" + sanitizeDomainForFile(domain) + ".jpg";
  if (Storage.exists(logoPath.c_str())) {
    outLogoPath = logoPath;
    return true;
  }

  // Tự động lấy Favicon/Logo chính thức từ tên miền qua Google Favicons + nén chuẩn 36x36 JPEG
  const std::string favSource = "https://www.google.com/s2/favicons?domain=" + domain + "&sz=64";
  const std::string proxyUrl = "https://wsrv.nl/?url=" + urlEncode(favSource) +
                               "&w=" + std::to_string(LOGO_SIZE) +
                               "&h=" + std::to_string(LOGO_SIZE) +
                               "&fit=contain&bg=white&output=jpg&q=85";

  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(3500);
  http.setUserAgent("Mozilla/5.0 (Linux; Android 10; Mobile) Chrome/120.0.0.0 Mobile Safari/537.36");
  if (!http.begin(proxyUrl)) return false;

  const int code = http.GET();
  if (code == 200) {
    std::string body = http.getString().c_str();
    if (body.length() > 64 && body.length() < 32000) {
      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(body.c_str());
      if (bytes[0] == 0xFF && bytes[1] == 0xD8) {
        HalFile f;
        if (Storage.openFileForWrite("WEB", logoPath, f)) {
          f.write(bytes, body.length());
          f.close();
          outLogoPath = logoPath;
          return true;
        }
      }
    }
  }
  return false;
}

int WebReaderActivity::getActiveContentFontId() const {
  if (webFontFamily == WebFontFamily::NotoSans) {
    switch (webFontSizeIdx) {
      case 0: return NOTOSANS_12_FONT_ID;
      case 2: return NOTOSANS_16_FONT_ID;
      case 3: return NOTOSANS_18_FONT_ID;
      case 1:
      default: return NOTOSANS_14_FONT_ID;
    }
  } else if (webFontFamily == WebFontFamily::ReaderCustom) {
    int id = SETTINGS.getReaderFontId();
    if (id != 0) return id;
  }
  // SystemUi
  switch (webFontSizeIdx) {
    case 0: return SMALL_FONT_ID;
    case 2: return UI_12_FONT_ID;
    case 3: return NOTOSANS_16_FONT_ID;
    case 1:
    default: return UI_10_FONT_ID;
  }
}

int WebReaderActivity::getActiveHeadingFontId() const {
  if (webFontFamily == WebFontFamily::NotoSans) {
    switch (webFontSizeIdx) {
      case 0: return NOTOSANS_14_FONT_ID;
      case 2: return NOTOSANS_18_FONT_ID;
      case 3: return NOTOSANS_18_FONT_ID;
      case 1:
      default: return NOTOSANS_16_FONT_ID;
    }
  } else if (webFontFamily == WebFontFamily::ReaderCustom) {
    int id = SETTINGS.getReaderFontId();
    if (id != 0) return id;
  }
  // SystemUi
  switch (webFontSizeIdx) {
    case 0: return UI_10_FONT_ID;
    case 2: return NOTOSANS_16_FONT_ID;
    case 3: return NOTOSANS_18_FONT_ID;
    case 1:
    default: return UI_12_FONT_ID;
  }
}

int WebReaderActivity::getActiveMarginPx() const {
  switch (webMarginIdx) {
    case 0: return 12;   // Hẹp
    case 2: return 30;   // Rộng
    case 1:
    default: return 18;  // Chuẩn
  }
}

int WebReaderActivity::getActiveLineHeight(bool isHeading) const {
  const int fontId = isHeading ? getActiveHeadingFontId() : getActiveContentFontId();
  int baseH = renderer.getLineHeight(fontId);
  if (baseH < 18) baseH = 20;
  int extraSpacing = 4;
  switch (webLineSpacingIdx) {
    case 0: extraSpacing = 1; break;  // Hẹp
    case 2: extraSpacing = 8; break;  // Rộng
    case 3: extraSpacing = 14; break; // Rất rộng
    case 1:
    default: extraSpacing = 4; break; // Chuẩn
  }
  return baseH + extraSpacing + (isHeading ? 4 : 0);
}

void WebReaderActivity::removeUnusedLogoFile(const Bookmark& removedBm) {
  std::string logoPath = removedBm.customLogoPath;
  if (logoPath.empty()) {
    const std::string dom = extractDomainFromUrl(removedBm.url);
    if (!dom.empty()) {
      logoPath = "/web_logo_" + sanitizeDomainForFile(dom) + ".jpg";
    }
  }
  if (logoPath.empty()) return;
  for (const auto& bm : bookmarks) {
    if (bm.customLogoPath == logoPath) return;
  }
  if (Storage.exists(logoPath.c_str())) {
    Storage.remove(logoPath.c_str());
  }
}

void WebReaderActivity::toggleBookmarkCurrentPage() {
  if (currentUrl.empty()) return;
  const std::string curDomain = extractDomainFromUrl(currentUrl);

  // Nếu trang đã có trong Trang chủ -> Bỏ lưu & xóa luôn file Logo của web đó
  for (auto it = bookmarks.begin(); it != bookmarks.end(); ++it) {
    if (it->url == currentUrl || (!curDomain.empty() && extractDomainFromUrl(it->url) == curDomain)) {
      if (bookmarks.size() > 1) {
        const Bookmark removed = *it;
        bookmarks.erase(it);
        removeUnusedLogoFile(removed);
        saveBookmarksToStorage();
      }
      return;
    }
  }

  // Chưa có -> Tự động tải Logo trang web về lưu trữ & thêm thẻ mới vào Trang chủ
  Bookmark newBm;
  newBm.name = makeFriendlySiteName(pageTitle, curDomain);
  newBm.url = currentUrl;
  newBm.subtitle = curDomain.empty() ? shortenUrlForBar(currentUrl) : curDomain;
  newBm.tag = "Đã lưu";
  newBm.logoIndex = detectBuiltinLogoIndex(curDomain);

  if (newBm.logoIndex < 0) {
    downloadWebsiteLogo(currentUrl, newBm.customLogoPath);
  }

  // Đưa trang mới lưu lên đầu danh sách Trang chủ (giữ gọn 6 thẻ trên lưới 2x3)
  bookmarks.insert(bookmarks.begin(), newBm);
  while (bookmarks.size() > 6) {
    const Bookmark evicted = bookmarks.back();
    bookmarks.pop_back();
    removeUnusedLogoFile(evicted);
  }
  saveBookmarksToStorage();
}

const char* WebReaderActivity::getSearchEngineName() const {
  switch (searchEngine) {
    case SearchEngine::Google:
      return "Google";
    case SearchEngine::DuckDuckGo:
      return "DuckDuckGo";
    case SearchEngine::Bing:
      return "Bing";
  }
  return "Google";
}

std::string WebReaderActivity::buildSearchUrl(const std::string& query) const {
  const std::string encoded = urlEncode(query);
  switch (searchEngine) {
    case SearchEngine::Google:
      return "https://www.google.com/search?hl=vi&q=" + encoded;
    case SearchEngine::DuckDuckGo:
      return "https://html.duckduckgo.com/html/?q=" + encoded;
    case SearchEngine::Bing:
      return "https://www.bing.com/search?q=" + encoded;
  }
  return "https://www.google.com/search?hl=vi&q=" + encoded;
}

void WebReaderActivity::cycleSearchEngine() {
  cacheJustCleared = false;
  if (searchEngine == SearchEngine::Google) {
    searchEngine = SearchEngine::DuckDuckGo;
  } else if (searchEngine == SearchEngine::DuckDuckGo) {
    searchEngine = SearchEngine::Bing;
  } else {
    searchEngine = SearchEngine::Google;
  }
}

std::string WebReaderActivity::getCookiesForUrl(const std::string& url) const {
  const std::string domain = extractDomainFromUrl(url);
  if (domain.empty()) return "";
  std::string result;
  for (const auto& entry : cookieJar) {
    if (domain == entry.domain ||
        (domain.size() > entry.domain.size() &&
         domain.compare(domain.size() - entry.domain.size(), entry.domain.size(), entry.domain) == 0)) {
      if (!result.empty()) result += "; ";
      result += entry.cookiePair;
    }
  }
  return result;
}

void WebReaderActivity::storeCookieFromHeader(const std::string& url, const std::string& setCookieHeader) {
  if (setCookieHeader.empty()) return;
  const std::string domain = extractDomainFromUrl(url);
  if (domain.empty()) return;

  const size_t semi = setCookieHeader.find(';');
  std::string pair = (semi != std::string::npos) ? setCookieHeader.substr(0, semi) : setCookieHeader;
  while (!pair.empty() && isspace(static_cast<unsigned char>(pair.front()))) pair.erase(0, 1);
  while (!pair.empty() && isspace(static_cast<unsigned char>(pair.back()))) pair.pop_back();
  const size_t eq = pair.find('=');
  if (eq == std::string::npos || eq == 0) return;

  const std::string key = pair.substr(0, eq);
  for (auto& existing : cookieJar) {
    if (existing.domain == domain && existing.cookiePair.rfind(key + "=", 0) == 0) {
      existing.cookiePair = pair;
      return;
    }
  }
  if (cookieJar.size() < 24) {
    cookieJar.push_back({domain, pair});
    totalCacheBytes += domain.size() + pair.size();
  }
}

void WebReaderActivity::clearAllBrowsingData() {
  cleanupCachedImages();
  cookieJar.clear();
  backStack.clear();
  forwardStack.clear();
  pendingHtml.clear();
  pendingBlocks.clear();
  pendingImageIndices.clear();
  totalCacheBytes = 0;
  cacheJustCleared = true;
}

std::string WebReaderActivity::formatCacheBadge() const {
  if (cacheJustCleared) {
    return "Đã dọn!";
  }
  const size_t totalItems = cookieJar.size() + backStack.size() + forwardStack.size() + static_cast<size_t>(cachedImageCount);
  if (totalCacheBytes == 0 && totalItems == 0) {
    return "Trống";
  }
  const size_t kb = std::max<size_t>(1, (totalCacheBytes + 1023) / 1024);
  return "Xóa " + std::to_string(kb) + "KB";
}

void WebReaderActivity::ensureWifiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_STA);
    const auto cred = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
    if (cred) {
      WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
    } else {
      WiFi.begin();
    }
  }
}

void WebReaderActivity::updateProgressImmediate(int progress) {
  loadingProgress = std::max(0, std::min(100, progress));
  isLoading = (loadingProgress > 0 && loadingProgress < 100);
  activityManager.requestUpdateAndWait();
}

void WebReaderActivity::onEnter() {
  Activity::onEnter();
  ensureWifiConnected();
  if (!currentUrl.empty()) {
    startLoadUrl(currentUrl);
  } else {
    state = State::Bookmarks;
  }
}

void WebReaderActivity::onExit() {
  Activity::onExit();
  cleanupCachedImages();
  pages.clear();
  pendingBlocks.clear();
  pendingImageIndices.clear();
  activeHitRegions.clear();
}

void WebReaderActivity::cleanupCachedImages() {
  for (int i = 0; i < 8; ++i) {
    std::string path = "/web_cache_" + std::to_string(i) + ".jpg";
    if (Storage.exists(path.c_str())) {
      Storage.remove(path.c_str());
    }
  }
  cachedImageCount = 0;
}

void WebReaderActivity::navigateTo(const std::string& url, bool pushHistory) {
  if (url.empty()) return;
  menuOpen = false;
  cacheJustCleared = false;
  if (pushHistory && state == State::Viewing && !currentUrl.empty() && currentUrl != url) {
    backStack.push_back(currentUrl);
    forwardStack.clear();
  }
  startLoadUrl(url);
}

void WebReaderActivity::goBack() {
  if (exitConfirmOpen) {
    exitConfirmOpen = false;
    requestUpdate(true);
    return;
  }
  if (bookmarkActionOpen) {
    bookmarkActionOpen = false;
    requestUpdate(true);
    return;
  }
  if (displayMenuOpen) {
    displayMenuOpen = false;
    requestUpdate(true);
    return;
  }
  if (menuOpen) {
    menuOpen = false;
    requestUpdate(true);
    return;
  }
  if (loadPhase != LoadPhase::Idle) {
    loadPhase = LoadPhase::Idle;
    isLoading = false;
    loadingProgress = 0;
    requestUpdate(true);
    return;
  }
  if (!backStack.empty()) {
    if (!currentUrl.empty()) {
      forwardStack.push_back(currentUrl);
    }
    std::string prev = backStack.back();
    backStack.pop_back();
    startLoadUrl(prev);
    return;
  }
  if (state == State::Viewing || state == State::Error) {
    state = State::Bookmarks;
    currentUrl.clear();
    requestUpdate(true);
    return;
  }
  // Đang ở Trang chủ và bấm nút Back -> Hiện hộp thoại xác nhận thoát!
  exitConfirmOpen = true;
  requestUpdate(true);
}

void WebReaderActivity::goForward() {
  menuOpen = false;
  if (!forwardStack.empty()) {
    if (!currentUrl.empty()) {
      backStack.push_back(currentUrl);
    }
    std::string next = forwardStack.back();
    forwardStack.pop_back();
    startLoadUrl(next);
  }
}

void WebReaderActivity::reloadPage() {
  menuOpen = false;
  if (!currentUrl.empty() && (state == State::Viewing || state == State::Error)) {
    startLoadUrl(currentUrl);
  } else {
    requestUpdate(true);
  }
}

void WebReaderActivity::startLoadUrl(const std::string& rawUrl) {
  if (rawUrl.empty()) return;
  ensureWifiConnected();

  std::string targetUrl = rawUrl;
  while (!targetUrl.empty() && isspace(static_cast<unsigned char>(targetUrl.front()))) targetUrl.erase(0, 1);
  while (!targetUrl.empty() && isspace(static_cast<unsigned char>(targetUrl.back()))) targetUrl.pop_back();

  if (targetUrl.find("://") == std::string::npos) {
    if (targetUrl.find('.') != std::string::npos && targetUrl.find(' ') == std::string::npos) {
      targetUrl = "https://" + targetUrl;
    } else {
      targetUrl = buildSearchUrl(targetUrl);
    }
  }

  targetUrl = unwrapSearchRedirectUrl(targetUrl);

  const size_t mWikiPos = targetUrl.find(".m.wikipedia.org");
  if (mWikiPos != std::string::npos) {
    targetUrl.replace(mWikiPos, 16, ".wikipedia.org");
  }
  if (targetUrl.find("wikipedia.org/wiki/") != std::string::npos &&
      targetUrl.find('?') == std::string::npos &&
      targetUrl.find('#') == std::string::npos) {
    targetUrl += "?useparsoid=0";
  }

  currentUrl = targetUrl;
  errorKind = WebErrorKind::None;
  errorHttpCode = 0;
  errorMessage.clear();
  menuOpen = false;
  cacheJustCleared = false;
  pendingHtml.clear();
  pendingBlocks.clear();
  pendingImageIndices.clear();
  nextImageToFetch = 0;

  loadPhase = LoadPhase::StartBar;
  updateProgressImmediate(20);
}

void WebReaderActivity::stepLoadPipeline() {
  if (loadPhase == LoadPhase::StartBar) {
    delay(45);
    loadPhase = LoadPhase::FetchHtml;
    updateProgressImmediate(42);
    return;
  }

  if (loadPhase == LoadPhase::FetchHtml) {
#if !TARGET_SIMULATOR
    if (WiFi.status() != WL_CONNECTED) {
      loadPhase = LoadPhase::Idle;
      isLoading = false;
      loadingProgress = 0;
      errorKind = WebErrorKind::NoWifi;
      errorHttpCode = 0;
      errorMessage = "Chưa kết nối Wi-Fi";
      state = State::Error;
      activityManager.requestUpdateAndWait();
      return;
    }
    if (ESP.getFreeHeap() < 35000) {
      loadPhase = LoadPhase::Idle;
      isLoading = false;
      loadingProgress = 0;
      errorKind = WebErrorKind::OutOfMemory;
      errorHttpCode = 0;
      errorMessage = "Cạn kiệt bộ nhớ RAM";
      state = State::Error;
      activityManager.requestUpdateAndWait();
      return;
    }
#endif
    std::string fetchUrl = currentUrl;
    int code = -1;
    std::string responseBody;

    auto performHttpGet = [&](const std::string& initialUrl, std::string& outBody, std::string& finalUrl) -> int {
      std::string urlToTry = initialUrl;
      int status = -1;
      for (int redirectCount = 0; redirectCount < 5; ++redirectCount) {
        freeink::SecureHttpClient http;
        http.setInsecure();
        http.setTimeout(12000);
        if (mobileMode) {
          http.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 16_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.0 Mobile/15E148 Safari/604.1");
          http.addHeader("Sec-CH-UA-Mobile", "?1");
          http.addHeader("Save-Data", "on");
        } else {
          http.setUserAgent("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36");
        }
        http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        http.addHeader("Accept-Language", "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");

        const std::string cookies = getCookiesForUrl(urlToTry);
        if (!cookies.empty()) {
          http.addHeader("Cookie", cookies);
        }

        if (!http.begin(urlToTry)) {
          return -1;
        }
        status = http.GET();

        std::string setCookie = http.getHeader("Set-Cookie");
        if (setCookie.empty()) setCookie = http.getHeader("set-cookie");
        if (!setCookie.empty()) {
          storeCookieFromHeader(urlToTry, setCookie);
        }

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
          std::string location = http.getHeader("Location");
          if (location.empty()) location = http.getHeader("location");
          std::string nextUrl;
          if (!location.empty() && freeink::SecureHttpClient::resolveUrl(urlToTry, location, nextUrl)) {
            urlToTry = nextUrl;
            finalUrl = nextUrl;
            continue;
          }
        }
        if (status == 200 || status == 202) {
          outBody = std::string(http.getString().c_str());
          status = 200;
        }
        break;
      }
      return status;
    };

    std::string resolvedFinal = fetchUrl;
    const std::string dom = extractDomainFromUrl(fetchUrl);
    if ((dom == "google.com" || dom == "google.com.vn") && fetchUrl.find("q=") == std::string::npos) {
      code = 200;
      responseBody =
          "<title>Google Tìm kiếm</title>"
          "<h1>Google Tìm kiếm</h1>"
          "<p>Chạm vào ô tìm kiếm phía trên hoặc thanh địa chỉ để nhập từ khóa tra cứu nhanh trên web.</p>";
    } else if ((dom == "google.com" || dom == "google.com.vn") && fetchUrl.find("q=") != std::string::npos) {
      const size_t qPos = fetchUrl.find("q=");
      std::string rawQ = fetchUrl.substr(qPos + 2);
      const size_t ampPos = rawQ.find('&');
      if (ampPos != std::string::npos) rawQ = rawQ.substr(0, ampPos);
      std::string dummyUrl;
      code = performHttpGet("https://html.duckduckgo.com/html/?q=" + rawQ, responseBody, dummyUrl);
      if (code != 200 || responseBody.empty()) {
        code = performHttpGet("https://www.bing.com/search?q=" + rawQ, responseBody, dummyUrl);
      }
    } else {
      code = performHttpGet(fetchUrl, responseBody, resolvedFinal);
      if (code == 200) {
        currentUrl = resolvedFinal;
      }
    }

    if (code == 200 && responseBody.size() > 400000) {
      loadPhase = LoadPhase::Idle;
      isLoading = false;
      loadingProgress = 0;
      errorKind = WebErrorKind::OutOfMemory;
      errorHttpCode = 200;
      errorMessage = "Dung lượng trang vượt giới hạn RAM (>400KB)";
      state = State::Error;
      activityManager.requestUpdateAndWait();
      return;
    }

    if (code == 200 && !responseBody.empty()) {
      totalCacheBytes += responseBody.size();
      pendingHtml = std::move(responseBody);
      loadPhase = LoadPhase::ParseHtml;
      updateProgressImmediate(68);
    } else {
      loadPhase = LoadPhase::Idle;
      isLoading = false;
      loadingProgress = 0;
      errorHttpCode = code;
      if (code == 404) {
        errorKind = WebErrorKind::NotFound404;
        errorMessage = "HTTP 404: Không tìm thấy trang";
      } else if (code == 403) {
        errorKind = WebErrorKind::Forbidden403;
        errorMessage = "HTTP 403: Bị từ chối truy cập";
      } else if (code >= 500 && code < 600) {
        errorKind = WebErrorKind::ServerError5xx;
        errorMessage = "HTTP " + std::to_string(code) + ": Lỗi máy chủ";
      } else if (code <= 0) {
        errorKind = WebErrorKind::ConnectionTimeout;
        errorMessage = "Hết thời gian chờ hoặc không thể kết nối";
      } else if (responseBody.empty()) {
        errorKind = WebErrorKind::EmptyContent;
        errorMessage = "Trang web rỗng không có nội dung";
      } else {
        errorKind = WebErrorKind::Generic;
        errorMessage = "Mã phản hồi HTTP " + std::to_string(code);
      }
      state = State::Error;
      activityManager.requestUpdateAndWait();
    }
    return;
  }

  if (loadPhase == LoadPhase::ParseHtml) {
    cleanupCachedImages();
    parseHtmlToBlocks(pendingHtml);
    pendingHtml.clear();

    if (pendingBlocks.empty()) {
      loadPhase = LoadPhase::Idle;
      isLoading = false;
      loadingProgress = 0;
      errorKind = WebErrorKind::EmptyContent;
      errorHttpCode = 200;
      errorMessage = "Trang web không có nội dung văn bản";
      state = State::Error;
      activityManager.requestUpdateAndWait();
      return;
    }

    if (imagesEnabled && !pendingImageIndices.empty()) {
      nextImageToFetch = 0;
      loadPhase = LoadPhase::FetchImages;
      updateProgressImmediate(76);
    } else {
      loadPhase = LoadPhase::Finalize;
      updateProgressImmediate(94);
    }
    return;
  }

  if (loadPhase == LoadPhase::FetchImages) {
    if (nextImageToFetch < pendingImageIndices.size() && cachedImageCount < MAX_ULTRA_LIGHT_IMAGES) {
      const size_t blkIdx = pendingImageIndices[nextImageToFetch++];
      if (blkIdx < pendingBlocks.size()) {
        std::string cachePath;
        int w = 0, h = 0;
        if (downloadUltraLightImage(pendingBlocks[blkIdx].pendingImageUrl, cachePath, w, h)) {
          pendingBlocks[blkIdx].imageCachePath = cachePath;
          pendingBlocks[blkIdx].imgW = w;
          pendingBlocks[blkIdx].imgH = h;
        }
      }
      const int totalImgs = std::max<int>(1, static_cast<int>(pendingImageIndices.size()));
      const int pct = 76 + (static_cast<int>(nextImageToFetch) * 18) / totalImgs;
      if (nextImageToFetch >= pendingImageIndices.size() || cachedImageCount >= MAX_ULTRA_LIGHT_IMAGES) {
        loadPhase = LoadPhase::Finalize;
      }
      updateProgressImmediate(std::min(95, pct));
      return;
    }
    loadPhase = LoadPhase::Finalize;
    updateProgressImmediate(95);
    return;
  }

  if (loadPhase == LoadPhase::Finalize) {
    delay(35);
    layoutBlocksToPages();
    pendingImageIndices.clear();
    state = State::Viewing;
    loadPhase = LoadPhase::Idle;
    isLoading = false;
    loadingProgress = 0;
    activityManager.requestUpdateAndWait();
    return;
  }
}

bool WebReaderActivity::downloadUltraLightImage(const std::string& imgUrl, std::string& outCachePath, int& outW, int& outH) {
  if (!imagesEnabled || cachedImageCount >= MAX_ULTRA_LIGHT_IMAGES) return false;
  if (isDecorativeOrTinyImageUrl(imgUrl)) return false;

  std::string cachePath = "/web_cache_" + std::to_string(cachedImageCount) + ".jpg";
  bool downloaded = false;

  // 1. Thử tải trực tiếp ảnh từ URL gốc (nhanh, trung bình 0.06s - 0.5s từ CDN báo chí)
  {
    std::string directUrl = imgUrl;
    for (int redir = 0; redir < 2; ++redir) {
      freeink::SecureHttpClient http;
      http.setInsecure();
      http.setTimeout(3500);
      http.setUserAgent("Mozilla/5.0 (Linux; Android 10; Mobile) Chrome/120.0.0.0 Mobile Safari/537.36");
      if (!http.begin(directUrl)) break;

      int code = http.GET();
      if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
        std::string loc = http.getHeader("Location");
        if (loc.empty()) loc = http.getHeader("location");
        std::string resolved;
        if (!loc.empty() && freeink::SecureHttpClient::resolveUrl(directUrl, loc, resolved)) {
          directUrl = resolved;
          continue;
        }
        break;
      }
      if (code == 200) {
        std::string body = http.getString().c_str();
        if (body.length() > 64 && body.length() <= 200000) {
          const uint8_t* bytes = reinterpret_cast<const uint8_t*>(body.c_str());
          if (bytes[0] == 0xFF && bytes[1] == 0xD8) {
            HalFile f;
            if (Storage.openFileForWrite("WEB", cachePath, f)) {
              f.write(bytes, body.length());
              f.close();
              ImageDimensions dims{};
              if (JpegToFramebufferConverter::getDimensionsStatic(cachePath, dims) &&
                  dims.width >= 32 && dims.height >= 24) {
                outCachePath = cachePath;
                outW = TARGET_IMG_W;
                outH = TARGET_IMG_H;
                cachedImageCount++;
                totalCacheBytes += body.length();
                downloaded = true;
                break;
              } else {
                Storage.remove(cachePath.c_str());
              }
            }
          }
        }
      }
      break;
    }
  }

  if (downloaded) return true;

  // 2. Fallback: Nếu tải trực tiếp không được hoặc ảnh cần resize -> dùng proxy wsrv.nl
  std::string proxyUrl = "https://wsrv.nl/?url=" + urlEncode(imgUrl) +
                         "&w=" + std::to_string(TARGET_IMG_W) +
                         "&h=" + std::to_string(TARGET_IMG_H) +
                         "&fit=cover&output=jpg&q=45";

  std::string currentTryUrl = proxyUrl;
  for (int redir = 0; redir < 2; ++redir) {
    freeink::SecureHttpClient http;
    http.setInsecure();
    http.setTimeout(5000);
    http.setUserAgent("Mozilla/5.0 (Linux; Android 10; Mobile) Chrome/120.0.0.0 Mobile Safari/537.36");
    if (!http.begin(currentTryUrl)) break;

    int code = http.GET();
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      std::string loc = http.getHeader("Location");
      if (loc.empty()) loc = http.getHeader("location");
      std::string resolved;
      if (!loc.empty() && freeink::SecureHttpClient::resolveUrl(currentTryUrl, loc, resolved)) {
        currentTryUrl = resolved;
        continue;
      }
      break;
    }
    if (code == 200) {
      std::string body = http.getString().c_str();
      if (body.length() > 64 && body.length() <= 200000) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(body.c_str());
        if (bytes[0] == 0xFF && bytes[1] == 0xD8) {
          HalFile f;
          if (Storage.openFileForWrite("WEB", cachePath, f)) {
            f.write(bytes, body.length());
            f.close();
            ImageDimensions dims{};
            if (JpegToFramebufferConverter::getDimensionsStatic(cachePath, dims) &&
                dims.width >= 32 && dims.height >= 24) {
              outCachePath = cachePath;
              outW = TARGET_IMG_W;
              outH = TARGET_IMG_H;
              cachedImageCount++;
              totalCacheBytes += body.length();
              downloaded = true;
            } else {
              Storage.remove(cachePath.c_str());
            }
          }
        }
      }
    }
    break;
  }
  return downloaded;
}

void WebReaderActivity::parseHtmlToBlocks(const std::string& html) {
  pages.clear();
  activeHitRegions.clear();
  pendingBlocks.clear();
  pendingImageIndices.clear();
  currentPage = 0;
  pageTitle.clear();

  const size_t titleStart = html.find("<title");
  if (titleStart != std::string::npos) {
    const size_t titleOpen = html.find('>', titleStart);
    const size_t titleEnd = html.find("</title>", titleOpen);
    if (titleOpen != std::string::npos && titleEnd != std::string::npos) {
      pageTitle = decodeHtmlEntities(html.substr(titleOpen + 1, titleEnd - titleOpen - 1));
    }
  }
  if (pageTitle.empty()) {
    pageTitle = shortenUrlForBar(currentUrl);
  }

  std::vector<RawBlock> rawBlocks;
  const bool isWiki = currentUrl.find("wikipedia.org") != std::string::npos;
  const bool isSearchEnginePage = currentUrl.find("duckduckgo.com") != std::string::npos ||
                                  currentUrl.find("google.") != std::string::npos ||
                                  currentUrl.find("bing.com") != std::string::npos;
  bool hasInputBox = false;

  if (isSearchEnginePage || isWiki) {
    std::string searchAction;
    if (isWiki) {
      searchAction = "https://vi.wikipedia.org/w/index.php?search=";
    } else if (currentUrl.find("duckduckgo.com") != std::string::npos) {
      searchAction = "https://html.duckduckgo.com/html/?q=";
    } else if (currentUrl.find("bing.com") != std::string::npos) {
      searchAction = "https://www.bing.com/search?q=";
    }
    rawBlocks.push_back({ItemType::InputBox, "Nhập từ khóa tìm kiếm...", searchAction, "", "", 0, 0});
    hasInputBox = true;
  }

  std::string currentText;
  std::string activeLinkUrl;
  std::string currentFormAction = currentUrl;
  std::string pendingPictureSrc;
  bool inHeading = false;
  bool inParagraph = false;
  bool inlineParagraphLink = false;
  bool inTag = false;
  char tagQuote = 0;
  std::string currentTag;
  bool skipContent = false;
  std::string skipTagName;
  int skipDepth = 0;
  std::string scriptBuffer;
  int queuedCandidateImages = 0;

  auto flushTextBlock = [&]() {
    while (!currentText.empty() && isspace(static_cast<unsigned char>(currentText.front()))) {
      currentText.erase(0, 1);
    }
    while (!currentText.empty() && isspace(static_cast<unsigned char>(currentText.back()))) {
      currentText.pop_back();
    }
    if (currentText.empty()) return;

    std::string decoded = decodeHtmlEntities(currentText);
    currentText.clear();
    while (!decoded.empty() && isspace(static_cast<unsigned char>(decoded.front()))) {
      decoded.erase(0, 1);
    }
    while (!decoded.empty() && isspace(static_cast<unsigned char>(decoded.back()))) {
      decoded.pop_back();
    }
    if (decoded.empty()) return;

    bool hasAlphaNum = false;
    for (unsigned char ch : decoded) {
      if (isalnum(ch) || ch >= 0x80) {
        if (decoded != "-" && decoded != "—" && decoded != "–" && decoded != "•" &&
            decoded != "Quảng cáo" && decoded != "sửa" && decoded != "[sửa]" && decoded != "sửa mã nguồn") {
          hasAlphaNum = true;
        }
        break;
      }
    }
    if (!hasAlphaNum) return;

    if (!rawBlocks.empty() && rawBlocks.back().text == decoded && rawBlocks.back().type != ItemType::Image) {
      return;
    }

    if (!activeLinkUrl.empty()) {
      const bool isBareUrl = decoded.find(' ') == std::string::npos;
      if (inHeading && !isBareUrl) {
        rawBlocks.push_back({ItemType::HeadingLink, decoded, activeLinkUrl, "", "", 0, 0});
      } else if (((inParagraph && decoded.size() >= 45) || decoded.size() > 95) && !isBareUrl) {
        rawBlocks.push_back({ItemType::SnippetLink, decoded, activeLinkUrl, "", "", 0, 0});
      } else if (decoded.size() >= 34 && !isBareUrl) {
        rawBlocks.push_back({ItemType::HeadingLink, decoded, activeLinkUrl, "", "", 0, 0});
      } else {
        rawBlocks.push_back({ItemType::Link, decoded, activeLinkUrl, "", "", 0, 0});
      }
    } else if (inHeading) {
      rawBlocks.push_back({ItemType::Heading, decoded, "", "", "", 0, 0});
    } else {
      rawBlocks.push_back({ItemType::Text, decoded, "", "", "", 0, 0});
    }
  };

  size_t parseStart = 0;
  if (isWiki) {
    const size_t mwPos = html.find("id=\"mw-content-text\"");
    if (mwPos != std::string::npos) {
      const size_t mwClose = html.find('>', mwPos);
      if (mwClose != std::string::npos) {
        parseStart = mwClose + 1;
      }
    }
  }
  const size_t parseLimit = std::min(html.size(), parseStart + 350000);

  for (size_t i = parseStart; i < parseLimit; ++i) {
    const char c = html[i];
    if (!inTag && i + 3 < parseLimit && c == '<' && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
      const size_t commentEnd = html.find("-->", i + 4);
      if (commentEnd != std::string::npos) {
        i = commentEnd + 2;
        continue;
      }
      break;
    }
    if (!inTag && c == '<') {
      inTag = true;
      tagQuote = 0;
      currentTag.clear();
      continue;
    }
    if (inTag) {
      const bool trackQuotes = !skipContent || (skipTagName != "script" && skipTagName != "style");
      if (trackQuotes) {
        if (tagQuote != 0) {
          if (c == tagQuote) tagQuote = 0;
          currentTag += c;
          continue;
        } else if (c == '"' || c == '\'') {
          tagQuote = c;
          currentTag += c;
          continue;
        }
      }
      if (c != '>') {
        currentTag += c;
        continue;
      }

      inTag = false;
      std::string tagLower = currentTag;
      std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);
      while (!tagLower.empty() && isspace(static_cast<unsigned char>(tagLower.front()))) {
        tagLower.erase(0, 1);
      }

      if (!tagLower.empty() && tagLower[0] == '/') {
        std::string closeTag = tagLower.substr(1);
        const size_t sp = closeTag.find_first_of(" \t\r\n");
        if (sp != std::string::npos) closeTag = closeTag.substr(0, sp);

        if (skipContent && closeTag == skipTagName) {
          if (--skipDepth <= 0) {
            skipContent = false;
            skipDepth = 0;
            if (jsEnabled && closeTag == "script" && !scriptBuffer.empty()) {
              for (const char* key : {"\"description\":\"", "\"articleBody\":\"", "\"headline\":\""}) {
                size_t kpos = scriptBuffer.find(key);
                if (kpos != std::string::npos) {
                  size_t vstart = kpos + std::strlen(key);
                  size_t vend = scriptBuffer.find('"', vstart);
                  if (vend != std::string::npos && vend - vstart > 20) {
                    std::string val = scriptBuffer.substr(vstart, std::min<size_t>(vend - vstart, 600));
                    rawBlocks.push_back({ItemType::Text, decodeHtmlEntities(val), "", "", "", 0, 0});
                  }
                }
              }
            }
            scriptBuffer.clear();
          }
          continue;
        }

        if (!skipContent) {
          if (closeTag == "a") {
            if (!inlineParagraphLink) {
              flushTextBlock();
              activeLinkUrl.clear();
            }
            inlineParagraphLink = false;
          } else if (closeTag == "picture") {
            pendingPictureSrc.clear();
          } else if (closeTag == "h1" || closeTag == "h2" || closeTag == "h3" || closeTag == "h4") {
            flushTextBlock();
            inHeading = false;
          } else if (closeTag == "p") {
            flushTextBlock();
            inParagraph = false;
            inlineParagraphLink = false;
            activeLinkUrl.clear();
          } else if (closeTag == "article") {
            flushTextBlock();
            if (!rawBlocks.empty() && rawBlocks.back().type != ItemType::Divider) {
              rawBlocks.push_back({ItemType::Divider, "", "", "", "", 0, 0});
            }
          } else if (closeTag == "div" || closeTag == "li" || closeTag == "tr" ||
                     closeTag == "section" || closeTag == "td") {
            flushTextBlock();
          }
        }
      } else {
        const size_t spacePos = tagLower.find_first_of(" \t\r\n/");
        std::string tagName = (spacePos != std::string::npos) ? tagLower.substr(0, spacePos) : tagLower;
        const bool selfClosing = !tagLower.empty() && tagLower.back() == '/';

        if (skipContent) {
          if (!selfClosing && tagName == skipTagName && skipTagName != "script" && skipTagName != "style") {
            skipDepth++;
          }
          continue;
        }

        if (tagName == "script" || tagName == "style" || tagName == "svg" ||
            tagName == "noscript" || tagName == "footer" || tagName == "head" ||
            tagName == "nav" || tagName == "header" || tagName == "aside" || tagName == "menu" ||
            tagName == "select" || tagName == "button" || tagName == "sup" || tagName == "cite" ||
            (isWiki && tagName == "table" && currentUrl.find("Trang_Ch") == std::string::npos)) {
          if (tagName != "sup") {
            flushTextBlock();
          }
          if (!selfClosing) {
            skipContent = true;
            skipTagName = tagName;
            skipDepth = 1;
            scriptBuffer.clear();
          }
        } else if (tagName == "a") {
          bool hasPriorParagraphText = false;
          if (inParagraph) {
            for (unsigned char ch : currentText) {
              if (!isspace(ch)) {
                hasPriorParagraphText = true;
                break;
              }
            }
          }
          if (hasPriorParagraphText) {
            inlineParagraphLink = true;
          } else {
            flushTextBlock();
            inlineParagraphLink = false;
            std::string href = extractAttr(currentTag, "href");
            if (jsEnabled && href.rfind("javascript:", 0) == 0) {
              size_t httpPos = href.find("http");
              if (httpPos != std::string::npos) {
                size_t endQ = href.find_first_of("'\"", httpPos);
                href = href.substr(httpPos, endQ == std::string::npos ? std::string::npos : endQ - httpPos);
              } else {
                href.clear();
              }
            }
            if (!href.empty() && href[0] != '#' && href.rfind("javascript:", 0) != 0) {
              std::string resolved;
              if (freeink::SecureHttpClient::resolveUrl(currentUrl, href, resolved)) {
                activeLinkUrl = unwrapSearchRedirectUrl(resolved);
              }
            }
          }
        } else if (tagName == "h1" || tagName == "h2" || tagName == "h3" || tagName == "h4") {
          flushTextBlock();
          inHeading = true;
        } else if (tagName == "p") {
          flushTextBlock();
          inParagraph = true;
          inlineParagraphLink = false;
        } else if (tagName == "form") {
          std::string action = extractAttr(currentTag, "action");
          if (!action.empty()) {
            std::string resolved;
            if (freeink::SecureHttpClient::resolveUrl(currentUrl, action, resolved)) {
              currentFormAction = resolved;
            }
          }
        } else if (tagName == "input") {
          std::string type = extractAttr(currentTag, "type");
          std::transform(type.begin(), type.end(), type.begin(), ::tolower);
          if (!hasInputBox && (type.empty() || type == "text" || type == "search")) {
            std::string name = extractAttr(currentTag, "name");
            std::string placeholder = extractAttr(currentTag, "placeholder");
            if (placeholder.empty()) placeholder = "Chạm để nhập từ khóa...";
            std::string queryUrl = currentFormAction;
            if (!name.empty()) {
              queryUrl += (queryUrl.find('?') == std::string::npos ? "?" : "&") + name + "=";
            }
            rawBlocks.push_back({ItemType::InputBox, placeholder, queryUrl, "", "", 0, 0});
            hasInputBox = true;
          }
        } else if (tagName == "source") {
          if (jsEnabled && pendingPictureSrc.empty()) {
            std::string srcset = extractAttr(currentTag, "data-srcset");
            if (srcset.empty()) srcset = extractAttr(currentTag, "srcset");
            if (!srcset.empty()) {
              size_t sp = srcset.find_first_of(" ,");
              pendingPictureSrc = (sp != std::string::npos) ? srcset.substr(0, sp) : srcset;
            }
          }
        } else if (tagName == "img") {
          flushTextBlock();
          std::string imgSrc;
          if (jsEnabled) {
            imgSrc = extractAttr(currentTag, "data-src");
            if (imgSrc.empty()) imgSrc = extractAttr(currentTag, "data-original");
            if (imgSrc.empty()) imgSrc = extractAttr(currentTag, "data-lazy-src");
            if (imgSrc.empty()) {
              std::string srcset = extractAttr(currentTag, "data-srcset");
              if (srcset.empty()) srcset = extractAttr(currentTag, "srcset");
              if (!srcset.empty()) {
                size_t sp = srcset.find_first_of(" ,");
                imgSrc = (sp != std::string::npos) ? srcset.substr(0, sp) : srcset;
              }
            }
            if (imgSrc.empty() && !pendingPictureSrc.empty()) {
              imgSrc = pendingPictureSrc;
            }
          }
          if (imgSrc.empty() || imgSrc.rfind("data:", 0) == 0) {
            imgSrc = extractAttr(currentTag, "src");
          }
          if ((imgSrc.empty() || imgSrc.rfind("data:", 0) == 0) && !pendingPictureSrc.empty()) {
            imgSrc = pendingPictureSrc;
          }
          pendingPictureSrc.clear();
          std::string alt = extractAttr(currentTag, "alt");

          if (imagesEnabled && !isSearchEnginePage && queuedCandidateImages < MAX_ULTRA_LIGHT_IMAGES &&
              !imgSrc.empty() && imgSrc.rfind("data:", 0) != 0) {
            bool tinyDimension = false;
            const std::string wAttr = extractAttr(currentTag, "width");
            const std::string hAttr = extractAttr(currentTag, "height");
            if (!wAttr.empty()) {
              long w = strtol(wAttr.c_str(), nullptr, 10);
              if (w > 0 && w < 48) tinyDimension = true;
            }
            if (!hAttr.empty()) {
              long h = strtol(hAttr.c_str(), nullptr, 10);
              if (h > 0 && h < 48) tinyDimension = true;
            }

            std::string resolvedImg;
            if (!tinyDimension && freeink::SecureHttpClient::resolveUrl(currentUrl, imgSrc, resolvedImg) &&
                !isDecorativeOrTinyImageUrl(resolvedImg)) {
              rawBlocks.push_back({ItemType::Image, alt, activeLinkUrl, resolvedImg, "", 0, 0});
              queuedCandidateImages++;
            }
          }
        } else if (tagName == "br" || tagName == "div" || tagName == "li" || tagName == "tr" || tagName == "hr") {
          flushTextBlock();
          if (tagName == "hr") {
            rawBlocks.push_back({ItemType::Divider, "", "", "", "", 0, 0});
          }
        }
      }
      continue;
    }

    if (skipContent) {
      if (jsEnabled && skipTagName == "script" && scriptBuffer.size() < 24000) {
        scriptBuffer += c;
      }
    } else {
      if (isspace(static_cast<unsigned char>(c))) {
        if (!currentText.empty() && currentText.back() != ' ') {
          currentText += ' ';
        }
      } else {
        currentText += c;
      }
    }
  }
  flushTextBlock();

  pendingBlocks.reserve(rawBlocks.size());
  size_t idx = 0;
  while (idx < rawBlocks.size()) {
    if (rawBlocks[idx].type == ItemType::Link) {
      size_t runEnd = idx;
      while (runEnd < rawBlocks.size() && rawBlocks[runEnd].type == ItemType::Link) {
        runEnd++;
      }
      if (runEnd - idx >= 4) {
        idx = runEnd;
        continue;
      }
    }
    if (rawBlocks[idx].type == ItemType::Image) {
      pendingImageIndices.push_back(pendingBlocks.size());
    }
    pendingBlocks.push_back(rawBlocks[idx]);
    if (rawBlocks[idx].type == ItemType::SnippetLink) {
      pendingBlocks.push_back({ItemType::Divider, "", "", "", "", 0, 0});
    }
    idx++;
  }
  if (pendingBlocks.empty() && !rawBlocks.empty()) {
    pendingBlocks = std::move(rawBlocks);
  }
}

void WebReaderActivity::layoutBlocksToPages() {
  pages.clear();
  activeHitRegions.clear();
  currentPage = 0;

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int marginPx = getActiveMarginPx();
  const int maxTextW = screenW - marginPx * 2 - 8;
  const int maxViewportH = screenH - 64 - 58;

  PageSlice currentSlice;
  int currentSliceH = 0;

  auto pushItem = [&](const LayoutItem& item) {
    if (currentSliceH + item.pixelHeight > maxViewportH && !currentSlice.items.empty()) {
      pages.push_back(std::move(currentSlice));
      currentSlice = PageSlice{};
      currentSliceH = 0;
    }
    currentSliceH += item.pixelHeight;
    currentSlice.items.push_back(item);
  };

  for (const auto& blk : pendingBlocks) {
    if (blk.type == ItemType::Image) {
      if (blk.imageCachePath.empty()) continue;
      LayoutItem imgItem;
      imgItem.type = ItemType::Image;
      imgItem.text = blk.text;
      imgItem.url = blk.url;
      imgItem.imageCachePath = blk.imageCachePath;
      imgItem.imageW = blk.imgW;
      imgItem.imageH = blk.imgH;
      imgItem.pixelHeight = blk.imgH + 14;
      pushItem(imgItem);
      continue;
    }

    if (blk.type == ItemType::InputBox) {
      LayoutItem inp;
      inp.type = ItemType::InputBox;
      inp.text = blk.text;
      inp.url = blk.url;
      inp.pixelHeight = 48;
      pushItem(inp);
      continue;
    }

    if (blk.type == ItemType::Divider) {
      if (currentSlice.items.empty() || currentSlice.items.back().type == ItemType::Divider) {
        continue;
      }
      LayoutItem div;
      div.type = ItemType::Divider;
      div.pixelHeight = 14;
      pushItem(div);
      continue;
    }

    const bool isHeading = (blk.type == ItemType::Heading || blk.type == ItemType::HeadingLink);
    const bool isBold = (isHeading || blk.type == ItemType::Link);
    const auto fontStyle = isBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int fontId = isHeading ? getActiveHeadingFontId() : getActiveContentFontId();
    const int linePixelH = getActiveLineHeight(isHeading);

    std::istringstream stream(blk.text);
    std::string word;
    std::string line;
    bool firstLine = true;

    const int availW = (blk.type == ItemType::HeadingLink || blk.type == ItemType::Link) ? (maxTextW - 18) : maxTextW;

    while (stream >> word) {
      if (renderer.getTextWidth(fontId, word.c_str(), fontStyle) > availW) {
        while (word.size() > 6 &&
               renderer.getTextWidth(fontId, (word + "...").c_str(), fontStyle) > availW) {
          word.pop_back();
          while (!word.empty() && (static_cast<unsigned char>(word.back()) & 0xC0) == 0x80) {
            word.pop_back();
          }
        }
        word += "...";
      }
      std::string testLine = line.empty() ? word : line + " " + word;
      if (renderer.getTextWidth(fontId, testLine.c_str(), fontStyle) <= availW) {
        line = testLine;
      } else {
        if (!line.empty()) {
          LayoutItem li;
          li.type = blk.type;
          li.text = line;
          li.url = blk.url;
          li.pixelHeight = linePixelH;
          li.isFirstLineOfLink = firstLine;
          pushItem(li);
          firstLine = false;
        }
        line = word;
      }
    }
    if (!line.empty()) {
      LayoutItem li;
      li.type = blk.type;
      li.text = line;
      li.url = blk.url;
      li.pixelHeight = linePixelH + 5;
      li.isFirstLineOfLink = firstLine;
      pushItem(li);
    }
  }

  if (!currentSlice.items.empty()) {
    pages.push_back(std::move(currentSlice));
  }

  if (pages.empty()) {
    PageSlice emptySlice;
    LayoutItem emptyItem;
    emptyItem.type = ItemType::Text;
    emptyItem.text = "Trang web không có nội dung văn bản hiển thị.";
    emptyItem.pixelHeight = 30;
    emptySlice.items.push_back(emptyItem);
    pages.push_back(std::move(emptySlice));
  }
}

void WebReaderActivity::promptEnterUrl(const std::string& prefill) {
  menuOpen = false;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_ENTER_URL), prefill,
                                                          256, InputType::Url);
  if (!keyboard) return;

  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data)) {
      if (!result.isCancelled && !keyboardResult->text.empty()) {
        navigateTo(keyboardResult->text, true);
      }
    }
  });
}

void WebReaderActivity::promptFormInput(const std::string& actionUrl) {
  menuOpen = false;
  auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, "Nhập từ khóa tìm kiếm", "",
                                                          256, InputType::Text);
  if (!keyboard) return;

  startActivityForResult(std::move(keyboard), [this, actionUrl](const ActivityResult& result) {
    if (const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data)) {
      if (!result.isCancelled && !keyboardResult->text.empty()) {
        if (actionUrl.empty()) {
          navigateTo(buildSearchUrl(keyboardResult->text), true);
        } else {
          navigateTo(actionUrl + urlEncode(keyboardResult->text), true);
        }
      }
    }
  });
}

void WebReaderActivity::nextPage() {
  if (currentPage < static_cast<int>(pages.size()) - 1) {
    currentPage++;
    requestUpdate(true);
  }
}

void WebReaderActivity::prevPage() {
  if (currentPage > 0) {
    currentPage--;
    requestUpdate(true);
  }
}

bool WebReaderActivity::handleMenuTouch(int x, int y) {
  if (!menuOpen) return false;

  constexpr int menuX = 124;
  constexpr int menuY = 64;
  constexpr int menuW = 344;
  constexpr int rowH = 42;
  constexpr int rowCount = 10;
  constexpr int menuH = rowCount * rowH + 12;

  if (x < menuX || x > menuX + menuW || y < menuY || y > menuY + menuH) {
    menuOpen = false;
    requestUpdate(true);
    return true;
  }

  const int row = (y - (menuY + 6)) / rowH;
  switch (row) {
    case 0:  // Tải lại trang
      reloadPage();
      break;
    case 1:  // Tiến tới trang sau (Forward)
      goForward();
      break;
    case 2:  // Về Trang chủ Trình duyệt
      menuOpen = false;
      state = State::Bookmarks;
      currentUrl.clear();
      requestUpdate(true);
      break;
    case 3:  // Lưu / Bỏ lưu trang web vào Trang chủ (kèm tự động tải Logo)
      cacheJustCleared = false;
      if (!currentUrl.empty()) {
        toggleBookmarkCurrentPage();
      }
      requestUpdate(true);
      break;
    case 4:  // Đổi Công cụ tìm kiếm (Google / DuckDuckGo / Bing)
      cycleSearchEngine();
      requestUpdate(true);
      break;
    case 5:  // Bật / Tắt JavaScript
      cacheJustCleared = false;
      jsEnabled = !jsEnabled;
      if (state == State::Viewing && !currentUrl.empty()) {
        reloadPage();
      } else {
        requestUpdate(true);
      }
      break;
    case 6:  // Bật / Tắt Ảnh siêu nhẹ
      cacheJustCleared = false;
      imagesEnabled = !imagesEnabled;
      if (state == State::Viewing && !currentUrl.empty()) {
        reloadPage();
      } else {
        requestUpdate(true);
      }
      break;
    case 7:  // Chế độ Mobile Classic / Desktop
      cacheJustCleared = false;
      mobileMode = !mobileMode;
      if (state == State::Viewing && !currentUrl.empty()) {
        reloadPage();
      } else {
        requestUpdate(true);
      }
      break;
    case 8:  // Dọn Cache & Cookie
      clearAllBrowsingData();
      requestUpdate(true);
      break;
    case 9:  // Thoát Trình duyệt về Kho Ứng Dụng -> Hiện dialog xác nhận!
      menuOpen = false;
      exitConfirmOpen = true;
      requestUpdate(true);
      break;
    default:
      break;
  }
  return true;
}

bool WebReaderActivity::handleToolbarTouch(int x, int y) {
  if (y < 0 || y > 62) return false;

  // 1. Nút Back [←] (x: 10..54)
  if (x >= 8 && x <= 56) {
    goBack();
    return true;
  }

  // 2. Nút Menu 3 gạch [☰] (x: 426..470)
  if (x >= 422 && x <= 474) {
    menuOpen = !menuOpen;
    if (menuOpen) cacheJustCleared = false;
    requestUpdate(true);
    return true;
  }

  // 3. Nút Reload [↻] bên phải trong thanh địa chỉ (x: 378..418)
  if (state == State::Viewing && !isLoading && x >= 378 && x <= 420) {
    reloadPage();
    return true;
  }

  // 4. Chạm Thanh Địa Chỉ & Tìm Kiếm (Omnibox Pill) (x: 60..420)
  if (x >= 58 && x <= 420) {
    promptEnterUrl(state == State::Viewing ? currentUrl : "");
    return true;
  }

  return false;
}

void WebReaderActivity::loop() {
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goBack();
    return;
  }

  if (loadPhase != LoadPhase::Idle) {
    stepLoadPipeline();
    return;
  }

  // 1. Kiểm tra Long-press (nhấn giữ) trên Bookmark card ở Màn hình chính
  int longX = 0, longY = 0;
  if (mappedInput.wasScreenLongPress(longX, longY)) {
    if (state == State::Bookmarks && !menuOpen && !exitConfirmOpen && !bookmarkActionOpen) {
      constexpr int startY = 104;
      constexpr int colW = 216;
      constexpr int cardH = 162;
      constexpr int gapX = 16;
      constexpr int gapY = 16;

      for (size_t i = 0; i < bookmarks.size() && i < 6; ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const int cardX = 16 + col * (colW + gapX);
        const int cardY = startY + row * (cardH + gapY);
        if (longX >= cardX && longX <= cardX + colW && longY >= cardY && longY <= cardY + cardH) {
          selectedBookmarkIdx = i;
          bookmarkActionOpen = true;
          requestUpdate(true);
          return;
        }
      }
    }
  }

  // 2. Chạm vào màn hình (Tap)
  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();

    // Hộp thoại xác nhận thoát được ưu tiên xử lý cao nhất
    if (exitConfirmOpen && handleExitConfirmTouch(touchX, touchY)) {
      return;
    }

    // Menu tùy chọn Bookmark (Truy cập, Sửa URL, Xóa)
    if (bookmarkActionOpen && handleBookmarkActionTouch(touchX, touchY)) {
      return;
    }

    // Menu Cài đặt Hiển thị Đọc sách (Display Settings Menu)
    if (displayMenuOpen && handleDisplayMenuTouch(touchX, touchY)) {
      return;
    }

    // Menu 3 gạch Hamburger
    if (menuOpen && handleMenuTouch(touchX, touchY)) {
      return;
    }

    // Thanh Toolbar trình duyệt ở trên cùng
    if (handleToolbarTouch(touchX, touchY)) {
      return;
    }

    if (state == State::Bookmarks) {
      constexpr int startY = 104;
      constexpr int colW = 216;
      constexpr int cardH = 162;
      constexpr int gapX = 16;
      constexpr int gapY = 16;

      for (size_t i = 0; i < bookmarks.size() && i < 6; ++i) {
        const int col = static_cast<int>(i % 2);
        const int row = static_cast<int>(i / 2);
        const int cardX = 16 + col * (colW + gapX);
        const int cardY = startY + row * (cardH + gapY);

        if (touchX >= cardX && touchX <= cardX + colW && touchY >= cardY && touchY <= cardY + cardH) {
          // Nếu chạm vào góc trên phải (nút ••• / tag), mở menu tùy chọn
          if (touchX >= cardX + colW - 54 && touchY <= cardY + 44) {
            selectedBookmarkIdx = i;
            bookmarkActionOpen = true;
            requestUpdate(true);
            return;
          }
          // Chạm vào thẻ -> truy cập trang web
          navigateTo(bookmarks[i].url, true);
          return;
        }
      }

      // Ô [+ Thêm trang web] nếu số bookmark < 6
      if (bookmarks.size() < 6) {
        const size_t emptyIdx = bookmarks.size();
        const int col = static_cast<int>(emptyIdx % 2);
        const int row = static_cast<int>(emptyIdx / 2);
        const int cardX = 16 + col * (colW + gapX);
        const int cardY = startY + row * (cardH + gapY);
        if (touchX >= cardX && touchX <= cardX + colW && touchY >= cardY && touchY <= cardY + cardH) {
          promptEnterUrl("");
          return;
        }
      }
    } else if (state == State::Viewing) {
      if (touchY >= screenH - 56) {
        // Nút trái: Trang trước
        if (touchX <= 164) {
          prevPage();
          return;
        }
        // Nút giữa: Mở Cài đặt Hiển thị Đọc sách [Aa • X / Y]
        if (touchX > 164 && touchX <= 315) {
          displayMenuOpen = !displayMenuOpen;
          requestUpdate(true);
          return;
        }
        // Nút phải: Trang sau
        if (touchX > 315) {
          nextPage();
          return;
        }
      }

      // Thanh trượt mép phải chạm nửa trên lùi, nửa dưới tiến
      if (touchX >= screenW - 22 && touchY > 62 && touchY < screenH - 56) {
        if (touchY < screenH / 2) {
          prevPage();
        } else {
          nextPage();
        }
        return;
      }

      // Chạm các link hoặc ô nhập liệu trong bài viết
      for (const auto& hit : activeHitRegions) {
        if (touchX >= hit.x && touchX <= hit.x + hit.w &&
            touchY >= hit.y && touchY <= hit.y + hit.h) {
          if (hit.isInputBox) {
            promptFormInput(hit.url);
            return;
          }
          if (!hit.url.empty()) {
            navigateTo(hit.url, true);
            return;
          }
        }
      }
    } else if (state == State::Error) {
      constexpr int btnW = 200;
      constexpr int btnH = 44;
      const int btnX = (screenW - btnW) / 2;
      constexpr int btnY = 380;

      constexpr int homeLinkW = 220;
      constexpr int homeLinkH = 36;
      const int homeLinkX = (screenW - homeLinkW) / 2;
      constexpr int homeLinkY = btnY + btnH + 18;

      if (touchX >= btnX && touchX <= btnX + btnW && touchY >= btnY && touchY <= btnY + btnH) {
        reloadPage();
        return;
      }
      if (touchX >= homeLinkX && touchX <= homeLinkX + homeLinkW &&
          touchY >= homeLinkY && touchY <= homeLinkY + homeLinkH) {
        state = State::Bookmarks;
        currentUrl.clear();
        requestUpdate(true);
        return;
      }
      // Tap outside buttons returns to Bookmarks
      state = State::Bookmarks;
      currentUrl.clear();
      requestUpdate(true);
      return;
    }
  }

  // Điều hướng bằng nút cứng hoặc thao tác vuốt
  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (state == State::Viewing) nextPage();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (state == State::Viewing) prevPage();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (state == State::Bookmarks) promptEnterUrl("");
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if ((swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Left) &&
      state == State::Viewing) {
    nextPage();
  } else if ((swipe == MappedInputManager::SwipeDir::Down || swipe == MappedInputManager::SwipeDir::Right) &&
             state == State::Viewing) {
    prevPage();
  }
}

void WebReaderActivity::render(RenderLock&&) {
  renderer.clearScreen();
  switch (state) {
    case State::Bookmarks:
      renderBookmarks();
      break;
    case State::Viewing:
      renderViewing();
      break;
    case State::Error:
      renderError();
      break;
  }
  if (menuOpen) {
    renderHamburgerMenu();
  }
  if (displayMenuOpen) {
    renderDisplaySettingsMenu();
  }
  if (bookmarkActionOpen) {
    renderBookmarkActionDialog();
  }
  if (exitConfirmOpen) {
    renderExitConfirmDialog();
  }

  if (requestFullRefresh) {
    requestFullRefresh = false;
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void WebReaderActivity::renderBrowserToolbar() {
  const int screenW = renderer.getScreenWidth();
  constexpr int barY = 8;
  constexpr int barH = 42;

  // 1. Nút Back [←] (44x42, viền 2px đồng bộ tuyệt đối với thanh địa chỉ và nút Menu)
  drawModernButton(renderer, 10, barY, 44, barH, 12, false, 2);
  drawLucideIcon(renderer, icon_browser_left_24, 20, barY + 9, false);

  // 2. Thanh Địa Chỉ & Tìm Kiếm Hiện Đại (Omnibox Pill 360x42, viền 2px)
  constexpr int omniX = 60;
  constexpr int omniW = 360;
  renderer.fillRoundedRect(omniX, barY, omniW, barH, 20, Color::White);
  renderer.drawRoundedRect(omniX, barY, omniW, barH, 2, 20, true);

  // Icon quả địa cầu ở Trang chủ, kính lúp khi đang duyệt web
  if (state == State::Bookmarks) {
    drawLucideIcon(renderer, icon_browser_globe_24, omniX + 12, barY + 9, false);
  } else {
    drawLucideIcon(renderer, icon_browser_search_24, omniX + 12, barY + 9, false);
  }

  std::string urlDisplay;
  if (isLoading) {
    urlDisplay = "Đang tải " + std::to_string(loadingProgress) + "% • " + shortenUrlForBar(currentUrl);
  } else if (state == State::Viewing && !currentUrl.empty()) {
    urlDisplay = shortenUrlForBar(currentUrl);
  } else {
    urlDisplay = std::string("Tìm với ") + getSearchEngineName() + " hoặc nhập địa chỉ...";
  }

  const int maxUrlW = (state == State::Viewing && !isLoading) ? (omniW - 82) : (omniW - 52);
  urlDisplay = fitTextToWidth(renderer, SMALL_FONT_ID, urlDisplay, maxUrlW);
  renderer.drawText(SMALL_FONT_ID, omniX + 40, barY + 12, urlDisplay.c_str(), true,
                    isLoading ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

  // Icon Reload [↻] 24x24 căn tâm đối xứng ở (omniX + omniW - 36, barY + 9)
  if (state == State::Viewing && !isLoading) {
    drawLucideIcon(renderer, icon_refresh_cw_24, omniX + omniW - 36, barY + 9, false);
  }

  // 3. Nút Menu 3 gạch [☰] (44x42, viền 2px đối xứng hoàn toàn với nút Back)
  constexpr int menuBtnX = 426;
  constexpr int menuBtnW = 44;
  drawModernButton(renderer, menuBtnX, barY, menuBtnW, barH, 12, menuOpen, 2);
  drawLucideIcon(renderer, icon_browser_menu_24, menuBtnX + 10, barY + 9, menuOpen);

  // 4. Cây Progress Loading ngay dưới chân chỗ điền địa chỉ (y = 53..59)
  if (isLoading && loadingProgress > 0) {
    constexpr int trackX = omniX + 10;
    constexpr int trackW = omniW - 20;
    constexpr int trackY = 53;
    constexpr int trackH = 6;
    renderer.fillRoundedRect(trackX, trackY, trackW, trackH, 3, Color::White);
    renderer.drawRoundedRect(trackX, trackY, trackW, trackH, 1, 3, true);
    const int fillW = std::max(14, (trackW * loadingProgress) / 100);
    renderer.fillRoundedRect(trackX, trackY, fillW, trackH, 3, Color::Black);
    renderer.drawLine(0, 61, screenW - 1, 61, 1, true);
  } else {
    renderer.drawLine(0, 58, screenW - 1, 58, 1, true);
  }
}

void WebReaderActivity::renderHamburgerMenu() {
  constexpr int menuX = 124;
  constexpr int menuY = 64;
  constexpr int menuW = 344;
  constexpr int rowH = 42;
  constexpr int rowCount = 10;
  constexpr int menuH = rowCount * rowH + 12;

  renderer.fillRoundedRect(menuX + 3, menuY + 3, menuW, menuH, 12, Color::Black);
  renderer.fillRoundedRect(menuX, menuY, menuW, menuH, 12, Color::White);
  renderer.drawRoundedRect(menuX, menuY, menuW, menuH, 2, 12, true);

  const std::string cacheBadge = formatCacheBadge();
  const bool bookmarked = isCurrentPageBookmarked();

  struct MenuRow {
    const char* label;
    const char* badge;
    bool badgeActive;
  };

  const MenuRow rows[rowCount] = {
      {"Tải lại trang web", "", false},
      {"Tiến tới trang sau", "", !forwardStack.empty()},
      {"Về Trang chủ", "", false},
      {"Lưu vào Trang chủ", bookmarked ? "Đã lưu" : "Lưu +", bookmarked},
      {"Công cụ tìm kiếm", getSearchEngineName(), true},
      {"Thực thi JavaScript", jsEnabled ? "BẬT" : "TẮT", jsEnabled},
      {"Tải ảnh siêu nhẹ", imagesEnabled ? "BẬT" : "TẮT", imagesEnabled},
      {"Định dạng trang web", mobileMode ? "Mobile" : "Desktop", mobileMode},
      {"Dọn Cache & Cookie", cacheBadge.c_str(), cacheJustCleared},
      {"Thoát Trình duyệt", "Thoát", false},
  };

  for (int i = 0; i < rowCount; ++i) {
    const int ry = menuY + 6 + i * rowH;

    renderer.drawText(UI_10_FONT_ID, menuX + 16, ry + 11, rows[i].label, true,
                      (i == 9) ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    const int badgeW = (i == 4 || i == 8) ? 104 : 82;
    constexpr int badgeH = 26;
    const int badgeX = menuX + menuW - badgeW - 14;
    const int badgeY = ry + (rowH - badgeH) / 2;
    const int iconCx = badgeX + badgeW / 2;
    const int iconCy = ry + rowH / 2;

    if (i == 0) {
      drawLucideIcon(renderer, icon_refresh_cw_24, iconCx - 12, iconCy - 12, false);
    } else if (i == 1) {
      drawLucideIcon(renderer, icon_browser_right_24, iconCx - 12, iconCy - 12, false);
    } else if (i == 2) {
      drawLucideIcon(renderer, icon_browser_home_24, iconCx - 12, iconCy - 12, false);
    } else if (i >= 3 && i <= 8) {
      drawModernButton(renderer, badgeX, badgeY, badgeW, badgeH, 12, rows[i].badgeActive);
      drawCenteredBoxText(renderer, SMALL_FONT_ID, badgeX, badgeY, badgeW, 4, rows[i].badge, !rows[i].badgeActive,
                          EpdFontFamily::BOLD);
    } else if (i == 9) {
      drawModernButton(renderer, badgeX, badgeY, badgeW, badgeH, 12, false);
      drawCenteredBoxText(renderer, SMALL_FONT_ID, badgeX, badgeY, badgeW, 4, rows[i].badge, true,
                          EpdFontFamily::BOLD);
    }

    if (i < rowCount - 1) {
      renderer.drawLine(menuX + 14, ry + rowH, menuX + menuW - 14, ry + rowH, 1, true);
    }
  }
}

void WebReaderActivity::renderBookmarks() {
  renderBrowserToolbar();

  // Tiêu đề gọn gàng bên trái kèm icon quả địa cầu và chỉ dẫn bấm giữ bên phải
  drawLucideIcon(renderer, icon_browser_globe_24, 18, 71, false);
  renderer.drawText(SMALL_FONT_ID, 48, 76, "TRANG WEB THƯỜNG DÙNG", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, 276, 76, "Giữ thẻ để sửa/xóa", true, EpdFontFamily::REGULAR);

  // Lưới Thẻ Trang Web Hiện Đại (2 cột x 3 hàng, toàn bộ Logo đều chuẩn kích thước 36x36)
  constexpr int startY = 104;
  constexpr int colW = 216;
  constexpr int cardH = 162;
  constexpr int gapX = 16;
  constexpr int gapY = 16;

  const freeink::Icon* brandLogos[6] = {
      &icon_logo_vnexpress_36,
      &icon_logo_wikipedia_36,
      &icon_logo_tuoitre_36,
      &icon_logo_dantri_36,
      &icon_logo_hackernews_36,
      &icon_logo_npr_36,
  };

  for (size_t i = 0; i < bookmarks.size() && i < 6; ++i) {
    const int col = static_cast<int>(i % 2);
    const int row = static_cast<int>(i / 2);
    const int cardX = 16 + col * (colW + gapX);
    const int cardY = startY + row * (cardH + gapY);

    // Hard shadow 2px & Rounded Rect
    renderer.fillRoundedRect(cardX + 2, cardY + 2, colW, cardH, 12, Color::Black);
    renderer.fillRoundedRect(cardX, cardY, colW, cardH, 12, Color::White);
    renderer.drawRoundedRect(cardX, cardY, colW, cardH, 1, 12, true);

    const int logoX = cardX + 16;
    const int logoY = cardY + 16;

    // Logo trang web
    if (bookmarks[i].logoIndex >= 0 && bookmarks[i].logoIndex < 6) {
      drawLucideIcon(renderer, *brandLogos[bookmarks[i].logoIndex], logoX, logoY, false);
    } else if (!bookmarks[i].customLogoPath.empty() && Storage.exists(bookmarks[i].customLogoPath.c_str())) {
      JpegToFramebufferConverter jpegDecoder;
      RenderConfig cfg{logoX, logoY, LOGO_SIZE, LOGO_SIZE};
      cfg.useExactDimensions = true;
      jpegDecoder.decodeToFramebuffer(bookmarks[i].customLogoPath, renderer, cfg);
      renderer.drawRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 1, 7, true);
    } else {
      renderer.fillRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 7, Color::White);
      renderer.drawRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 1, 7, true);
      drawLucideIcon(renderer, icon_browser_globe_24, logoX + 6, logoY + 6, false);
    }

    // Nút / Nhãn danh mục nhỏ góc trên phải kèm biểu tượng tùy chọn •••
    const int tagW = renderer.getTextWidth(SMALL_FONT_ID, bookmarks[i].tag.c_str()) + 16;
    const int tagX = cardX + colW - tagW - 12;
    const int tagY = cardY + 18;
    renderer.drawRoundedRect(tagX, tagY, tagW, 26, 1, 13, true);
    drawCenteredBoxText(renderer, SMALL_FONT_ID, tagX, tagY, tagW, 3, bookmarks[i].tag.c_str(), true);

    // Tên trang web lớn rõ ràng
    std::string safeName = fitTextToWidth(renderer, UI_12_FONT_ID, bookmarks[i].name, colW - 32);
    renderer.drawText(UI_12_FONT_ID, cardX + 16, cardY + 76, safeName.c_str(), true, EpdFontFamily::BOLD);

    // Địa chỉ tên miền gọn phía dưới
    std::string safeSub = fitTextToWidth(renderer, SMALL_FONT_ID, bookmarks[i].subtitle, colW - 32);
    renderer.drawText(SMALL_FONT_ID, cardX + 16, cardY + 118, safeSub.c_str());
  }

  // Thẻ trống [+ Thêm trang web] nếu còn chỗ trống
  if (bookmarks.size() < 6) {
    const size_t emptyIdx = bookmarks.size();
    const int col = static_cast<int>(emptyIdx % 2);
    const int row = static_cast<int>(emptyIdx / 2);
    const int cardX = 16 + col * (colW + gapX);
    const int cardY = startY + row * (cardH + gapY);

    renderer.fillRoundedRect(cardX, cardY, colW, cardH, 12, Color::White);
    renderer.drawRoundedRect(cardX, cardY, colW, cardH, 1, 12, true);

    // Vòng tròn dấu + ở giữa
    constexpr int plusSize = 44;
    const int plusX = cardX + (colW - plusSize) / 2;
    const int plusY = cardY + 28;
    renderer.drawRoundedRect(plusX, plusY, plusSize, plusSize, 1, plusSize / 2, true);
    drawCenteredBoxText(renderer, UI_12_FONT_ID, plusX, plusY, plusSize, 6, "+", true, EpdFontFamily::BOLD);

    drawCenteredBoxText(renderer, UI_10_FONT_ID, cardX, cardY + 88, colW, 4, "+ Thêm trang web", true, EpdFontFamily::BOLD);
    drawCenteredBoxText(renderer, SMALL_FONT_ID, cardX, cardY + 118, colW, 4, "Chạm để lưu web mới", true, EpdFontFamily::REGULAR);
  }
}

void WebReaderActivity::renderViewing() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  renderBrowserToolbar();
  activeHitRegions.clear();

  const int marginX = getActiveMarginPx();
  const int contentFontId = getActiveContentFontId();
  const int headingFontId = getActiveHeadingFontId();
  const int maxTextW = screenW - marginX * 2 - 8;

  if (!pages.empty() && currentPage < static_cast<int>(pages.size())) {
    const auto& slice = pages[currentPage];
    int y = 68;

    for (const auto& item : slice.items) {
      if (y + item.pixelHeight > screenH - 58) break;

      if (item.type == ItemType::InputBox) {
        const int boxX = marginX;
        const int boxW = screenW - marginX * 2 - 8;
        constexpr int boxH = 40;
        renderer.fillRoundedRect(boxX, y, boxW, boxH, 10, Color::White);
        renderer.drawRoundedRect(boxX, y, boxW, boxH, 1, 10, true);

        std::string label = fitTextToWidth(renderer, SMALL_FONT_ID, item.text, boxW - 68);
        renderer.drawText(SMALL_FONT_ID, boxX + 14, y + 12, label.c_str());
        renderer.fillRoundedRect(boxX + boxW - 54, y + 6, 48, boxH - 12, 8, Color::Black);
        drawCenteredBoxText(renderer, SMALL_FONT_ID, boxX + boxW - 54, y + 6, 48, 6, "Tìm", false, EpdFontFamily::BOLD);

        activeHitRegions.push_back({boxX, y, boxW, boxH, item.url, true});
        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::Image) {
        if (item.imageCachePath.empty() || !Storage.exists(item.imageCachePath.c_str())) {
          continue;
        }
        const int imgW = item.imageW > 0 ? item.imageW : TARGET_IMG_W;
        const int imgH = item.imageH > 0 ? item.imageH : TARGET_IMG_H;
        const int imgX = (screenW - 10 - imgW) / 2;

        renderer.drawRect(imgX - 1, y - 1, imgW + 2, imgH + 2, 1, true);

        JpegToFramebufferConverter jpegDecoder;
        RenderConfig cfg{imgX, y, imgW, imgH};
        cfg.useExactDimensions = true;
        jpegDecoder.decodeToFramebuffer(item.imageCachePath, renderer, cfg);

        if (!item.url.empty()) {
          activeHitRegions.push_back({imgX, y, imgW, imgH, item.url, false});
        }

        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::Divider) {
        renderer.drawLine(marginX, y + 6, screenW - marginX - 10, y + 6, 1, true);
        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::Heading) {
        renderer.drawText(headingFontId, marginX, y, item.text.c_str(), true, EpdFontFamily::BOLD);
        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::HeadingLink) {
        if (item.isFirstLineOfLink) {
          renderer.fillRoundedRect(marginX, y + 5, 6, 14, 2, Color::Black);
        }
        renderer.drawText(headingFontId, marginX + 12, y, item.text.c_str(), true, EpdFontFamily::BOLD);
        activeHitRegions.push_back({marginX, y - 2, maxTextW, item.pixelHeight, item.url, false});
        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::SnippetLink) {
        renderer.drawText(contentFontId, marginX, y, item.text.c_str(), true, EpdFontFamily::REGULAR);
        activeHitRegions.push_back({marginX, y - 2, maxTextW, item.pixelHeight, item.url, false});
        y += item.pixelHeight;
        continue;
      }

      if (item.type == ItemType::Link) {
        if (item.isFirstLineOfLink) {
          renderer.drawText(contentFontId, marginX, y, "•", true, EpdFontFamily::BOLD);
        }
        renderer.drawText(contentFontId, marginX + 12, y, item.text.c_str(), true, EpdFontFamily::BOLD);
        const int tw = std::min(maxTextW - 16, renderer.getTextWidth(contentFontId, item.text.c_str(), EpdFontFamily::BOLD));
        renderer.drawLine(marginX + 12, y + 21, marginX + 12 + tw, y + 21, 1, true);
        activeHitRegions.push_back({marginX, y - 2, tw + 24, item.pixelHeight, item.url, false});
        y += item.pixelHeight;
        continue;
      }

      // Văn bản bình thường
      if (webFocusReading) {
        renderBionicText(renderer, contentFontId, marginX, y, item.text);
      } else {
        renderer.drawText(contentFontId, marginX, y, item.text.c_str());
      }
      y += item.pixelHeight;
    }
  }

  // Thanh Cuộn Dọc mảnh ở mép phải
  constexpr int sbX = 470;
  constexpr int sbTop = 64;
  const int sbBottom = screenH - 58;
  const int sbTotalH = sbBottom - sbTop;
  renderer.drawLine(sbX + 2, sbTop, sbX + 2, sbBottom, 1, true);
  const int totalPages = std::max(1, static_cast<int>(pages.size()));
  const int thumbH = std::max(28, sbTotalH / totalPages);
  const int thumbY = sbTop + (totalPages > 1 ? (currentPage * (sbTotalH - thumbH)) / (totalPages - 1) : 0);
  renderer.fillRoundedRect(sbX, thumbY, 5, thumbH, 2, Color::Black);

  // Thanh Điều Hướng Đáy hiện đại: 3 nút lớn với icon và nhãn căn tâm hoàn hảo
  const int barY = screenH - 48;
  renderer.drawLine(0, barY - 6, screenW - 1, barY - 6, 1, true);

  // 1. Nút [ ^ Trang trước ] (rộng 148px, cao 40px)
  constexpr int leftBtnX = 12;
  constexpr int leftBtnW = 148;
  const bool leftDisabled = (currentPage <= 0);
  drawModernButton(renderer, leftBtnX, barY, leftBtnW, 40, 10, false, leftDisabled ? 1 : 2);
  const int prevTextW = renderer.getTextWidth(SMALL_FONT_ID, "Trang trước", EpdFontFamily::BOLD);
  constexpr int iconBoxW = 20;
  const int prevGroupW = iconBoxW + 6 + prevTextW;
  const int prevStartX = leftBtnX + (leftBtnW - prevGroupW) / 2;
  drawLucideIcon(renderer, icon_browser_up_24, prevStartX - 2, barY + 8, false);
  renderer.drawText(SMALL_FONT_ID, prevStartX + iconBoxW + 4, barY + 12, "Trang trước", true,
                    leftDisabled ? EpdFontFamily::REGULAR : EpdFontFamily::BOLD);

  // 2. Nút [ Aa • X / Y ] ở giữa (rộng 144px, cao 40px) - Bấm mở Cài đặt Hiển thị
  constexpr int midBtnX = 168;
  constexpr int midBtnW = 144;
  char midBuf[48];
  snprintf(midBuf, sizeof(midBuf), "Aa • %d / %d", currentPage + 1, totalPages);
  drawModernButton(renderer, midBtnX, barY, midBtnW, 40, 10, displayMenuOpen, 2);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, midBtnX, barY, midBtnW, 12, midBuf, !displayMenuOpen, EpdFontFamily::BOLD);

  // 3. Nút [ Trang sau v ] (rộng 148px, cao 40px)
  constexpr int rightBtnX = 320;
  constexpr int rightBtnW = 148;
  const bool rightDisabled = (currentPage >= totalPages - 1);
  drawModernButton(renderer, rightBtnX, barY, rightBtnW, 40, 10, true, 2);
  const int nextTextW = renderer.getTextWidth(SMALL_FONT_ID, "Trang sau", EpdFontFamily::BOLD);
  const int nextGroupW = nextTextW + 6 + iconBoxW;
  const int nextStartX = rightBtnX + (rightBtnW - nextGroupW) / 2;
  renderer.drawText(SMALL_FONT_ID, nextStartX, barY + 12, "Trang sau", false, EpdFontFamily::BOLD);
  drawLucideIcon(renderer, icon_browser_down_24, nextStartX + nextTextW + 4, barY + 8, true);
}

void WebReaderActivity::renderError() {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  renderBrowserToolbar();

  // Nền trắng phẳng sạch sẽ chuẩn trình duyệt hiện đại
  renderer.fillRect(0, 56, screenW, screenH - 56, false);

  // 1. Biểu tượng trang bị lỗi tối giản (Icon tờ giấy gập góc có mặt buồn `:(`)
  constexpr int iconW = 48;
  constexpr int iconH = 58;
  const int iconX = (screenW - iconW) / 2;
  constexpr int iconY = 160;

  // Viền tờ giấy bo nhẹ, góc trên bên phải bị gập (dog-ear fold)
  constexpr int fold = 14;
  renderer.drawLine(iconX, iconY + 4, iconX, iconY + iconH - 4, 1, true); // trái
  renderer.drawLine(iconX + 4, iconY + iconH, iconX + iconW - 4, iconY + iconH, 1, true); // đáy
  renderer.drawLine(iconX + iconW, iconY + fold, iconX + iconW, iconY + iconH - 4, 1, true); // phải
  renderer.drawLine(iconX + 4, iconY, iconX + iconW - fold, iconY, 1, true); // đỉnh trái

  // Nét gập góc tam giác
  renderer.drawLine(iconX + iconW - fold, iconY, iconX + iconW, iconY + fold, 1, true);
  renderer.drawLine(iconX + iconW - fold, iconY, iconX + iconW - fold, iconY + fold, 1, true);
  renderer.drawLine(iconX + iconW - fold, iconY + fold, iconX + iconW, iconY + fold, 1, true);

  // 4 góc bo nhẹ
  renderer.drawPixel(iconX + 1, iconY + 1, true);
  renderer.drawPixel(iconX + 1, iconY + iconH - 2, true);
  renderer.drawPixel(iconX + iconW - 2, iconY + iconH - 2, true);

  // Mặt buồn `:(` bên trong tờ giấy
  // Hai mắt
  renderer.fillRect(iconX + 14, iconY + 22, 4, 4, true);
  renderer.fillRect(iconX + 30, iconY + 22, 4, 4, true);
  // Miệng buồn cong xuống
  renderer.drawLine(iconX + 16, iconY + 40, iconX + 32, iconY + 40, 1, true);
  renderer.drawPixel(iconX + 14, iconY + 42, true);
  renderer.drawPixel(iconX + 34, iconY + 42, true);

  // 2. Tiêu đề ngắn gọn và Mô tả súc tích
  const char* titleText = "Không thể mở trang web này";
  std::string descText = "Đã xảy ra sự cố trong khi tải trang này.";
  const char* errorCodeText = "Mã lỗi: ERR_FAILED";

  switch (errorKind) {
    case WebErrorKind::OutOfMemory:
      titleText = "Ôi, hỏng!";
      descText = "Trang web này quá lớn khiến bộ nhớ của máy bị quá tải.";
      errorCodeText = "Mã lỗi: OUT_OF_MEMORY";
      break;
    case WebErrorKind::NotFound404:
      titleText = "Không tìm thấy trang";
      descText = "Không thể tìm thấy URL được yêu cầu trên máy chủ này.";
      errorCodeText = "Mã lỗi: ERR_HTTP_404_NOT_FOUND";
      break;
    case WebErrorKind::Forbidden403:
      titleText = "Quyền truy cập bị từ chối";
      descText = "Máy chủ từ chối phục vụ yêu cầu từ thiết bị đọc sách.";
      errorCodeText = "Mã lỗi: ERR_HTTP_403_FORBIDDEN";
      break;
    case WebErrorKind::ServerError5xx:
      titleText = "Máy chủ gặp sự cố";
      descText = "Hệ thống trang web đích đang bảo trì hoặc tạm thời quá tải.";
      errorCodeText = "Mã lỗi: ERR_HTTP_500_INTERNAL_SERVER_ERROR";
      break;
    case WebErrorKind::ConnectionTimeout:
      titleText = "Không thể kết nối";
      descText = "Máy chủ phản hồi quá lâu hoặc đường truyền mạng bị ngắt.";
      errorCodeText = "Mã lỗi: ERR_CONNECTION_TIMED_OUT";
      break;
    case WebErrorKind::NoWifi:
      titleText = "Không có kết nối Internet";
      descText = "Vui lòng kiểm tra lại kết nối Wi-Fi trên thiết bị đọc sách.";
      errorCodeText = "Mã lỗi: ERR_INTERNET_DISCONNECTED";
      break;
    case WebErrorKind::EmptyContent:
      titleText = "Trang không có nội dung";
      descText = "Trình duyệt không tìm thấy khối bài viết tĩnh để hiển thị.";
      errorCodeText = "Mã lỗi: ERR_EMPTY_RESPONSE";
      break;
    default:
      titleText = "Không thể mở trang web này";
      if (!errorMessage.empty()) descText = errorMessage;
      errorCodeText = "Mã lỗi: ERR_UNKNOWN_FAILED";
      break;
  }

  // Tiêu đề lớn
  renderer.drawCenteredText(UI_12_FONT_ID, 244, titleText, true, EpdFontFamily::BOLD);

  // Mô tả ngắn gọn (1-2 dòng)
  std::vector<std::string> descLines;
  wrapTextToLines(renderer, UI_10_FONT_ID, descText, screenW - 80, descLines);
  int curDescY = 284;
  for (const auto& line : descLines) {
    renderer.drawCenteredText(UI_10_FONT_ID, curDescY, line.c_str(), true);
    curDescY += 24;
  }

  // Mã lỗi kỹ thuật nhỏ gọn thanh lịch (chuẩn Chrome)
  renderer.drawCenteredText(SMALL_FONT_ID, 336, errorCodeText, true);

  // 3. Nút bấm chính [ Tải Lại Trang ] (Bo góc nhẹ, đen viền mỏng thanh lịch)
  constexpr int btnW = 200;
  constexpr int btnH = 44;
  const int btnX = (screenW - btnW) / 2;
  constexpr int btnY = 380;

  renderer.fillRoundedRect(btnX, btnY, btnW, btnH, 8, Color::Black);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, btnX, btnY, btnW, 11, "Tải lại trang", false, EpdFontFamily::BOLD);

  // 4. Liên kết phụ [ Quay về Trang chủ ]
  constexpr int homeLinkY = btnY + btnH + 24;
  const char* homeLabel = "Quay về Trang chủ";
  const int homeLabelW = renderer.getTextWidth(UI_10_FONT_ID, homeLabel);
  const int homeLinkX = (screenW - homeLabelW) / 2;
  renderer.drawText(UI_10_FONT_ID, homeLinkX, homeLinkY, homeLabel, true);
  renderer.drawLine(homeLinkX, homeLinkY + 18, homeLinkX + homeLabelW, homeLinkY + 18, 1, true);
}

void WebReaderActivity::renderDisplaySettingsMenu() {
  const int screenW = renderer.getScreenWidth();
  constexpr int menuW = 456;
  constexpr int menuH = 444;
  const int menuX = (screenW - menuW) / 2; // 12
  constexpr int menuY = 296;

  // Neo-Brutalist Hard Shadow & Rounded Card
  renderer.fillRoundedRect(menuX + 4, menuY + 4, menuW, menuH, 14, Color::Black);
  renderer.fillRoundedRect(menuX, menuY, menuW, menuH, 14, Color::White);
  renderer.drawRoundedRect(menuX, menuY, menuW, menuH, 2, 14, true);

  // 1. Header: Icon Aa + Tiêu đề + Nút [×]
  drawLucideIcon(renderer, icon_reader_text_24, menuX + 16, menuY + 10, false);
  renderer.drawText(UI_10_FONT_ID, menuX + 48, menuY + 13, "CÀI ĐẶT HIỂN THỊ ĐỌC SÁCH", true, EpdFontFamily::BOLD);

  // Nút [×] đóng ở góc phải
  drawModernButton(renderer, menuX + menuW - 44, menuY + 8, 32, 28, 8, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + menuW - 44, menuY + 8, 32, 5, "×", true, EpdFontFamily::BOLD);
  renderer.drawLine(menuX + 12, menuY + 42, menuX + menuW - 12, menuY + 42, 1, true);

  // 2. Chuyển trang (Jump Page): [ << ] [ -5 ] [ Trang X / Y ] [ +5 ] [ >> ]
  const int totalPages = std::max(1, static_cast<int>(pages.size()));
  constexpr int jumpY = menuY + 50;
  constexpr int jumpH = 36;
  // Button 1: [ << ]
  drawModernButton(renderer, menuX + 14, jumpY, 56, jumpH, 8, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 14, jumpY, 56, 9, "<<", true, EpdFontFamily::BOLD);
  // Button 2: [ -5 ]
  drawModernButton(renderer, menuX + 76, jumpY, 62, jumpH, 8, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 76, jumpY, 62, 9, "-5", true, EpdFontFamily::BOLD);
  // Button 3: [ Trang %d / %d ]
  char jumpBuf[40];
  snprintf(jumpBuf, sizeof(jumpBuf), "Trang %d / %d", currentPage + 1, totalPages);
  drawModernButton(renderer, menuX + 144, jumpY, 168, jumpH, 8, true, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 144, jumpY, 168, 9, jumpBuf, false, EpdFontFamily::BOLD);
  // Button 4: [ +5 ]
  drawModernButton(renderer, menuX + 318, jumpY, 62, jumpH, 8, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 318, jumpY, 62, 9, "+5", true, EpdFontFamily::BOLD);
  // Button 5: [ >> ]
  drawModernButton(renderer, menuX + 386, jumpY, 56, jumpH, 8, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 386, jumpY, 56, 9, ">>", true, EpdFontFamily::BOLD);

  // 3. Phông chữ: [ Noto Sans (Sách) ] | [ Hệ thống (UI) ]
  constexpr int fontY = menuY + 98;
  renderer.drawText(SMALL_FONT_ID, menuX + 16, fontY + 9, "Phông:", true, EpdFontFamily::BOLD);
  const bool isNoto = (webFontFamily == WebFontFamily::NotoSans);
  drawModernButton(renderer, menuX + 80, fontY, 176, 34, 8, isNoto, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 80, fontY, 176, 8, "Noto Sans (Sách)", !isNoto, EpdFontFamily::BOLD);
  drawModernButton(renderer, menuX + 264, fontY, 178, 34, 8, !isNoto, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 264, fontY, 178, 8, "Hệ thống (UI)", isNoto, EpdFontFamily::BOLD);

  // 4. Cỡ chữ: 12pt, 14pt, 16pt, 18pt
  constexpr int sizeY = menuY + 144;
  renderer.drawText(SMALL_FONT_ID, menuX + 16, sizeY + 9, "Cỡ chữ:", true, EpdFontFamily::BOLD);
  const char* sizeLabels[4] = {"12pt", "14pt", "16pt", "18pt"};
  for (int s = 0; s < 4; ++s) {
    const int sx = menuX + 80 + s * 90;
    const int sw = (s == 3) ? 92 : 82;
    const bool sel = (webFontSizeIdx == s);
    drawModernButton(renderer, sx, sizeY, sw, 34, 8, sel, 1);
    drawCenteredBoxText(renderer, SMALL_FONT_ID, sx, sizeY, sw, 8, sizeLabels[s], !sel, EpdFontFamily::BOLD);
  }

  // 5. Giãn dòng: Hẹp, Chuẩn, Rộng, Rất rộng
  constexpr int lineY = menuY + 190;
  renderer.drawText(SMALL_FONT_ID, menuX + 16, lineY + 9, "Dòng:", true, EpdFontFamily::BOLD);
  const char* lineLabels[4] = {"Hẹp", "Chuẩn", "Rộng", "R.rộng"};
  for (int l = 0; l < 4; ++l) {
    const int lx = menuX + 80 + l * 90;
    const int lw = (l == 3) ? 92 : 82;
    const bool sel = (webLineSpacingIdx == l);
    drawModernButton(renderer, lx, lineY, lw, 34, 8, sel, 1);
    drawCenteredBoxText(renderer, SMALL_FONT_ID, lx, lineY, lw, 8, lineLabels[l], !sel, EpdFontFamily::BOLD);
  }

  // 6. Lề trang: 12px, 18px, 30px
  constexpr int marginY = menuY + 236;
  renderer.drawText(SMALL_FONT_ID, menuX + 16, marginY + 9, "Lề:", true, EpdFontFamily::BOLD);
  const char* marginLabels[3] = {"Hẹp (12px)", "Chuẩn (18px)", "Rộng (30px)"};
  constexpr int mwList[3] = {114, 118, 120};
  int curMx = menuX + 80;
  for (int m = 0; m < 3; ++m) {
    const bool sel = (webMarginIdx == m);
    drawModernButton(renderer, curMx, marginY, mwList[m], 34, 8, sel, 1);
    drawCenteredBoxText(renderer, SMALL_FONT_ID, curMx, marginY, mwList[m], 8, marginLabels[m], !sel, EpdFontFamily::BOLD);
    curMx += mwList[m] + 8;
  }

  // 7. Focus Reading & Full E-ink Refresh
  constexpr int actionY = menuY + 284;
  // Left: Focus Reading toggle
  drawModernButton(renderer, menuX + 14, actionY, 208, 42, 10, webFocusReading, 2);
  const char* focusLabel = webFocusReading ? "Focus Đọc: BẬT" : "Focus Đọc: TẮT";
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 14, actionY, 208, 12, focusLabel, !webFocusReading, EpdFontFamily::BOLD);

  // Right: Làm mới mực E-ink
  drawModernButton(renderer, menuX + 234, actionY, 208, 42, 10, false, 2);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, menuX + 234, actionY, 208, 12, "Làm mới E-ink", true, EpdFontFamily::BOLD);

  // 8. Nút [ Đóng Cài Đặt ] lớn ở chân menu
  constexpr int closeBtnY = menuY + 382;
  drawModernButton(renderer, menuX + 14, closeBtnY, menuW - 28, 44, 10, false, 2);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, menuX + 14, closeBtnY, menuW - 28, 13, "Đóng Cài Đặt", true, EpdFontFamily::BOLD);
}

bool WebReaderActivity::handleDisplayMenuTouch(int x, int y) {
  constexpr int menuW = 456;
  constexpr int menuH = 444;
  constexpr int menuX = 12;
  constexpr int menuY = 296;

  // Nếu bấm ra ngoài khung menu -> đóng menu
  if (x < menuX || x > menuX + menuW || y < menuY || y > menuY + menuH) {
    displayMenuOpen = false;
    requestUpdate(true);
    return true;
  }

  // Nút [×] ở header hoặc nút [Đóng Cài Đặt] ở chân menu
  if ((x >= menuX + menuW - 48 && x <= menuX + menuW - 10 && y >= menuY + 6 && y <= menuY + 40) ||
      (y >= menuY + 380 && y <= menuY + 430)) {
    displayMenuOpen = false;
    requestUpdate(true);
    return true;
  }

  // Section 2: Jump Page
  constexpr int jumpY = menuY + 50;
  if (y >= jumpY && y <= jumpY + 38) {
    const int totalPages = std::max(1, static_cast<int>(pages.size()));
    if (x >= menuX + 14 && x < menuX + 74) {
      // [ << ] Trang đầu
      currentPage = 0;
      requestUpdate(true);
      return true;
    }
    if (x >= menuX + 76 && x < menuX + 142) {
      // [ -5 ] Lùi 5 trang
      currentPage = std::max(0, currentPage - 5);
      requestUpdate(true);
      return true;
    }
    if (x >= menuX + 144 && x < menuX + 316) {
      // [ Trang X / Y ] Chạm vào để nhập số trang
      displayMenuOpen = false;
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
          renderer, mappedInput, "Nhập số trang (1 - " + std::to_string(totalPages) + ")",
          std::to_string(currentPage + 1), 8, InputType::Text);
      if (keyboard) {
        startActivityForResult(std::move(keyboard), [this, totalPages](const ActivityResult& result) {
          if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
            if (!result.isCancelled && !kr->text.empty()) {
              int p = atoi(kr->text.c_str());
              if (p >= 1 && p <= totalPages) {
                currentPage = p - 1;
                requestUpdate(true);
              }
            }
          }
        });
      }
      return true;
    }
    if (x >= menuX + 318 && x < menuX + 384) {
      // [ +5 ] Tiến 5 trang
      currentPage = std::min(totalPages - 1, currentPage + 5);
      requestUpdate(true);
      return true;
    }
    if (x >= menuX + 386 && x <= menuX + 444) {
      // [ >> ] Trang cuối
      currentPage = totalPages - 1;
      requestUpdate(true);
      return true;
    }
  }

  // Section 3: Phông chữ
  constexpr int fontY = menuY + 98;
  if (y >= fontY && y <= fontY + 36) {
    if (x >= menuX + 80 && x < menuX + 260) {
      webFontFamily = WebFontFamily::NotoSans;
      layoutBlocksToPages();
      requestFullRefresh = true;
      requestUpdate(true);
      return true;
    }
    if (x >= menuX + 264 && x <= menuX + 444) {
      webFontFamily = WebFontFamily::SystemUi;
      layoutBlocksToPages();
      requestFullRefresh = true;
      requestUpdate(true);
      return true;
    }
  }

  // Section 4: Cỡ chữ
  constexpr int sizeY = menuY + 144;
  if (y >= sizeY && y <= sizeY + 36) {
    for (int s = 0; s < 4; ++s) {
      const int sx = menuX + 80 + s * 90;
      const int sw = (s == 3) ? 92 : 82;
      if (x >= sx && x <= sx + sw) {
        webFontSizeIdx = s;
        layoutBlocksToPages();
        requestFullRefresh = true;
        requestUpdate(true);
        return true;
      }
    }
  }

  // Section 5: Giãn dòng
  constexpr int lineY = menuY + 190;
  if (y >= lineY && y <= lineY + 36) {
    for (int l = 0; l < 4; ++l) {
      const int lx = menuX + 80 + l * 90;
      const int lw = (l == 3) ? 92 : 82;
      if (x >= lx && x <= lx + lw) {
        webLineSpacingIdx = l;
        layoutBlocksToPages();
        requestFullRefresh = true;
        requestUpdate(true);
        return true;
      }
    }
  }

  // Section 6: Lề trang
  constexpr int marginY = menuY + 236;
  if (y >= marginY && y <= marginY + 36) {
    constexpr int mwList[3] = {114, 118, 120};
    int curMx = menuX + 80;
    for (int m = 0; m < 3; ++m) {
      if (x >= curMx && x <= curMx + mwList[m]) {
        webMarginIdx = m;
        layoutBlocksToPages();
        requestFullRefresh = true;
        requestUpdate(true);
        return true;
      }
      curMx += mwList[m] + 8;
    }
  }

  // Section 7: Focus Reading & Full E-ink Refresh
  constexpr int actionY = menuY + 284;
  if (y >= actionY && y <= actionY + 44) {
    if (x >= menuX + 14 && x < menuX + 224) {
      // Toggle Focus Reading
      webFocusReading = !webFocusReading;
      requestUpdate(true);
      return true;
    }
    if (x >= menuX + 234 && x <= menuX + 444) {
      // Full E-ink Refresh
      requestFullRefresh = true;
      requestUpdate(true);
      return true;
    }
  }

  return true;
}

void WebReaderActivity::renderExitConfirmDialog() {
  const int screenW = renderer.getScreenWidth();
  constexpr int dialogW = 420;
  constexpr int dialogH = 210;
  const int dialogX = (screenW - dialogW) / 2; // 30
  constexpr int dialogY = 295;

  // Neo-Brutalist Hard Drop Shadow (4px black) & Rounded Card (radius 14, border 2px)
  renderer.fillRoundedRect(dialogX + 4, dialogY + 4, dialogW, dialogH, 14, Color::Black);
  renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, 14, Color::White);
  renderer.drawRoundedRect(dialogX, dialogY, dialogW, dialogH, 2, 14, true);

  // Title: "Thoát Trình duyệt?"
  drawCenteredBoxText(renderer, UI_12_FONT_ID, dialogX, dialogY + 24, dialogW, 4, "Thoát Trình duyệt?", true, EpdFontFamily::BOLD);

  // Subtitle / Confirmation Text
  drawCenteredBoxText(renderer, UI_10_FONT_ID, dialogX, dialogY + 68, dialogW, 4, "Bạn có muốn đóng trình duyệt web", true, EpdFontFamily::REGULAR);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, dialogX, dialogY + 96, dialogW, 4, "và quay lại Màn hình chính?", true, EpdFontFamily::REGULAR);

  // 2 nút chọn: [ Ở lại ] và [ Thoát ]
  constexpr int btnY = dialogY + 142;
  constexpr int btnH = 46;
  constexpr int btnW = 180;

  // Nút trái: [ Ở lại ]
  drawModernButton(renderer, dialogX + 20, btnY, btnW, btnH, 10, false, 2);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, dialogX + 20, btnY, btnW, 14, "Ở lại", true, EpdFontFamily::BOLD);

  // Nút phải: [ Thoát ] (Inverted black button)
  drawModernButton(renderer, dialogX + 220, btnY, btnW, btnH, 10, true, 2);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, dialogX + 220, btnY, btnW, 14, "Thoát", false, EpdFontFamily::BOLD);
}

bool WebReaderActivity::handleExitConfirmTouch(int x, int y) {
  constexpr int dialogW = 420;
  constexpr int dialogH = 210;
  constexpr int dialogX = 30;
  constexpr int dialogY = 295;

  // Chạm ra ngoài khung -> đóng dialog
  if (x < dialogX || x > dialogX + dialogW || y < dialogY || y > dialogY + dialogH) {
    exitConfirmOpen = false;
    requestUpdate(true);
    return true;
  }

  constexpr int btnY = dialogY + 142;
  constexpr int btnH = 46;

  // Nút [ Ở lại ]
  if (x >= dialogX + 20 && x <= dialogX + 200 && y >= btnY && y <= btnY + btnH) {
    exitConfirmOpen = false;
    requestUpdate(true);
    return true;
  }

  // Nút [ Thoát ]
  if (x >= dialogX + 220 && x <= dialogX + 400 && y >= btnY && y <= btnY + btnH) {
    exitConfirmOpen = false;
    cleanupCachedImages();
    finish();
    return true;
  }

  return true;
}

void WebReaderActivity::renderBookmarkActionDialog() {
  if (selectedBookmarkIdx >= bookmarks.size()) return;
  const auto& bm = bookmarks[selectedBookmarkIdx];

  const int screenW = renderer.getScreenWidth();
  constexpr int dialogW = 420;
  constexpr int dialogH = 340;
  const int dialogX = (screenW - dialogW) / 2; // 30
  constexpr int dialogY = 230;

  // Neo-Brutalist Hard Drop Shadow (4px black) & Rounded Card (radius 14, border 2px)
  renderer.fillRoundedRect(dialogX + 4, dialogY + 4, dialogW, dialogH, 14, Color::Black);
  renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, 14, Color::White);
  renderer.drawRoundedRect(dialogX, dialogY, dialogW, dialogH, 2, 14, true);

  // 1. Header: Logo (36x36) + Tên bookmark + Domain
  const int logoX = dialogX + 20;
  const int logoY = dialogY + 16;
  const freeink::Icon* brandLogos[6] = {
      &icon_logo_vnexpress_36,
      &icon_logo_wikipedia_36,
      &icon_logo_tuoitre_36,
      &icon_logo_dantri_36,
      &icon_logo_hackernews_36,
      &icon_logo_npr_36,
  };

  if (bm.logoIndex >= 0 && bm.logoIndex < 6) {
    drawLucideIcon(renderer, *brandLogos[bm.logoIndex], logoX, logoY, false);
  } else if (!bm.customLogoPath.empty() && Storage.exists(bm.customLogoPath.c_str())) {
    JpegToFramebufferConverter jpegDecoder;
    RenderConfig cfg{logoX, logoY, LOGO_SIZE, LOGO_SIZE};
    cfg.useExactDimensions = true;
    jpegDecoder.decodeToFramebuffer(bm.customLogoPath, renderer, cfg);
    renderer.drawRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 1, 7, true);
  } else {
    renderer.fillRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 7, Color::White);
    renderer.drawRoundedRect(logoX, logoY, LOGO_SIZE, LOGO_SIZE, 1, 7, true);
    drawLucideIcon(renderer, icon_browser_globe_24, logoX + 6, logoY + 6, false);
  }

  std::string safeName = fitTextToWidth(renderer, UI_12_FONT_ID, bm.name, dialogW - 90);
  renderer.drawText(UI_12_FONT_ID, dialogX + 68, dialogY + 18, safeName.c_str(), true, EpdFontFamily::BOLD);
  std::string safeSub = fitTextToWidth(renderer, SMALL_FONT_ID, bm.subtitle, dialogW - 90);
  renderer.drawText(SMALL_FONT_ID, dialogX + 68, dialogY + 40, safeSub.c_str());

  renderer.drawLine(dialogX + 16, dialogY + 62, dialogX + dialogW - 16, dialogY + 62, 1, true);

  // 2. Nút 1: [ Truy cập trang web ]
  const int btnX = dialogX + 20;
  constexpr int btnW = dialogW - 40;
  constexpr int btnH = 46;

  // Nút Truy cập (Inverted primary)
  drawModernButton(renderer, btnX, dialogY + 74, btnW, btnH, 10, true, 2);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, btnX, dialogY + 74, btnW, 14, "Truy cập trang web", false, EpdFontFamily::BOLD);

  // Nút 2: [ Sửa địa chỉ URL ]
  drawModernButton(renderer, btnX, dialogY + 132, btnW, btnH, 10, false, 2);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, btnX, dialogY + 132, btnW, 14, "Sửa địa chỉ URL", true, EpdFontFamily::BOLD);

  // Nút 3: [ Xóa khỏi Trang chủ ]
  drawModernButton(renderer, btnX, dialogY + 190, btnW, btnH, 10, false, 2);
  drawLucideIcon(renderer, icon_trash_2_24, btnX + 32, dialogY + 201, false);
  drawCenteredBoxText(renderer, UI_10_FONT_ID, btnX + 24, dialogY + 190, btnW - 24, 14, "Xóa khỏi Trang chủ", true, EpdFontFamily::BOLD);

  // Nút 4: [ Đóng ]
  drawModernButton(renderer, btnX, dialogY + 264, btnW, 44, 10, false, 1);
  drawCenteredBoxText(renderer, SMALL_FONT_ID, btnX, dialogY + 264, btnW, 13, "Đóng", true, EpdFontFamily::BOLD);
}

bool WebReaderActivity::handleBookmarkActionTouch(int x, int y) {
  constexpr int dialogW = 420;
  constexpr int dialogH = 340;
  constexpr int dialogX = 30;
  constexpr int dialogY = 230;

  // Chạm ra ngoài khung -> đóng dialog
  if (x < dialogX || x > dialogX + dialogW || y < dialogY || y > dialogY + dialogH) {
    bookmarkActionOpen = false;
    requestUpdate(true);
    return true;
  }

  constexpr int btnX = dialogX + 20;
  constexpr int btnW = dialogW - 40;
  constexpr int btnH = 46;

  // Nút 1: [ Truy cập trang web ]
  if (x >= btnX && x <= btnX + btnW && y >= dialogY + 74 && y <= dialogY + 74 + btnH) {
    bookmarkActionOpen = false;
    if (selectedBookmarkIdx < bookmarks.size()) {
      navigateTo(bookmarks[selectedBookmarkIdx].url, true);
    }
    return true;
  }

  // Nút 2: [ Sửa địa chỉ URL ]
  if (x >= btnX && x <= btnX + btnW && y >= dialogY + 132 && y <= dialogY + 132 + btnH) {
    bookmarkActionOpen = false;
    if (selectedBookmarkIdx < bookmarks.size()) {
      std::string oldUrl = bookmarks[selectedBookmarkIdx].url;
      const size_t editIdx = selectedBookmarkIdx;
      auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(
          renderer, mappedInput, "Sửa địa chỉ URL", oldUrl, 256, InputType::Url);
      if (keyboard) {
        startActivityForResult(std::move(keyboard), [this, editIdx](const ActivityResult& result) {
          if (const auto* kr = std::get_if<KeyboardResult>(&result.data)) {
            if (!result.isCancelled && !kr->text.empty() && editIdx < bookmarks.size()) {
              bookmarks[editIdx].url = kr->text;
              bookmarks[editIdx].subtitle = shortenUrlForBar(kr->text);
              saveBookmarksToStorage();
              requestUpdate(true);
            }
          }
        });
      }
    }
    return true;
  }

  // Nút 3: [ Xóa khỏi Trang chủ ]
  if (x >= btnX && x <= btnX + btnW && y >= dialogY + 190 && y <= dialogY + 190 + btnH) {
    if (selectedBookmarkIdx < bookmarks.size()) {
      removeUnusedLogoFile(bookmarks[selectedBookmarkIdx]);
      bookmarks.erase(bookmarks.begin() + selectedBookmarkIdx);
      saveBookmarksToStorage();
    }
    bookmarkActionOpen = false;
    requestFullRefresh = true;
    requestUpdate(true);
    return true;
  }

  // Nút 4: [ Đóng ]
  if (x >= btnX && x <= btnX + btnW && y >= dialogY + 264 && y <= dialogY + 308) {
    bookmarkActionOpen = false;
    requestUpdate(true);
    return true;
  }

  return true;
}
