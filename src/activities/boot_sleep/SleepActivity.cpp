#include "SleepActivity.h"

#include <BitmapHelpers.h>
#include <Epub.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>
#include <PNGdec.h>
#include <Xtc.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include <HalClock.h>
#include <HalPowerManager.h>
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
#include "activities/reader/ReaderUtils.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"
#include "util/LunarCalendar.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "util/BookProgress.h"

namespace {
// Metalio: B/W sleep images use the full 0xF7 waveform instead of HALF (two
// 0xFC partials), matching the clean pass before absolute gray below.
#if FREEINK_DEVICE_METALIO_EINK4
constexpr auto kSleepClean = HalDisplay::FULL_REFRESH;
#else
constexpr auto kSleepClean = HalDisplay::HALF_REFRESH;
#endif

// The absolute gray paint has no base pass. On Metalio the prior screen shows
// through it after the panel sits unpowered, so clean with 0xF7 first.
void cleanBeforeAbsoluteGray(GfxRenderer& renderer) {
#if FREEINK_DEVICE_METALIO_EINK4
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
#else
  (void)renderer;
#endif
}

HalDisplay::GrayscaleMode sleepGrayscaleMode(const GfxRenderer& renderer) {
  return renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Direct).supported()
             ? HalDisplay::GrayscaleMode::Direct
             : HalDisplay::GrayscaleMode::Absolute;
}

// Kept separate from /sleep.bmp and /.sleep so alpha-overlay art does not mix with full-screen wallpapers.
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr char TRANSPARENT_SLEEP_DIR[] = "/.sleep-overlay";
constexpr char TRANSPARENT_SLEEP_LEGACY_DIR[] = "/sleep-overlay";
constexpr size_t MAX_SLEEP_FILE_NAME_LEN = 256;
constexpr uint8_t MIN_VISIBLE_ALPHA = 8;

struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

struct OverlayBmpInfo {
  int width = 0;
  int height = 0;
  bool topDown = false;
  uint32_t dataOffset = 0;
  uint32_t rowBytes = 0;
};

uint16_t readLE16(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  return static_cast<uint16_t>(b0) | (static_cast<uint16_t>(b1) << 8);
}

uint32_t readLE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  const auto b0 = static_cast<uint8_t>(c0 < 0 ? 0 : c0);
  const auto b1 = static_cast<uint8_t>(c1 < 0 ? 0 : c1);
  const auto b2 = static_cast<uint8_t>(c2 < 0 ? 0 : c2);
  const auto b3 = static_cast<uint8_t>(c3 < 0 ? 0 : c3);
  return static_cast<uint32_t>(b0) | (static_cast<uint32_t>(b1) << 8) | (static_cast<uint32_t>(b2) << 16) |
         (static_cast<uint32_t>(b3) << 24);
}

uint32_t readBE32(HalFile& file) {
  const int c0 = file.read();
  const int c1 = file.read();
  const int c2 = file.read();
  const int c3 = file.read();
  if (c0 < 0 || c1 < 0 || c2 < 0 || c3 < 0) return 0;
  return (static_cast<uint32_t>(c0) << 24) | (static_cast<uint32_t>(c1) << 16) | (static_cast<uint32_t>(c2) << 8) |
         static_cast<uint32_t>(c3);
}

bool isValidPngHeader(HalFile& file) {
  static constexpr uint8_t PNG_SIGNATURE[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  static constexpr uint32_t MAX_SOURCE_PIXELS = 2048u * 1536u;
  uint8_t signature[8];
  if (!file.seek(0) || file.read(signature, sizeof(signature)) != static_cast<int>(sizeof(signature)) ||
      !std::equal(std::begin(signature), std::end(signature), std::begin(PNG_SIGNATURE))) {
    return false;
  }

  const uint32_t ihdrLength = readBE32(file);
  char chunkType[4];
  if (file.read(reinterpret_cast<uint8_t*>(chunkType), sizeof(chunkType)) != static_cast<int>(sizeof(chunkType)) ||
      ihdrLength != 13 || !std::equal(std::begin(chunkType), std::end(chunkType), "IHDR")) {
    return false;
  }

  const uint32_t width = readBE32(file);
  const uint32_t height = readBE32(file);
  const int bitDepth = file.read();
  const int colorType = file.read();
  const int compression = file.read();
  const int filter = file.read();
  const int interlace = file.read();

  const bool supportedBitDepth =
      bitDepth == 8 || ((colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_INDEXED) &&
                        (bitDepth == 1 || bitDepth == 2 || bitDepth == 4));
  const bool supportedColorType = colorType == PNG_PIXEL_GRAYSCALE || colorType == PNG_PIXEL_TRUECOLOR ||
                                  colorType == PNG_PIXEL_INDEXED || colorType == PNG_PIXEL_GRAY_ALPHA ||
                                  colorType == PNG_PIXEL_TRUECOLOR_ALPHA;
  return width > 0 && height > 0 && width <= 2048 && height <= 3072 && width * height <= MAX_SOURCE_PIXELS &&
         supportedBitDepth && supportedColorType && compression == 0 && filter == 0 && interlace == 0;
}

BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

bool parseOverlayBmpHeader(HalFile& file, OverlayBmpInfo& info, const bool logErrors) {
  if (!file) return false;
  if (!file.seek(0)) return false;

  if (readLE16(file) != 0x4D42) {
    if (logErrors) LOG_ERR("SLP", "Transparent overlay is not a BMP");
    return false;
  }

  file.seekCur(8);
  info.dataOffset = readLE32(file);

  const uint32_t dibSize = readLE32(file);
  if (dibSize < 40) {
    if (logErrors) LOG_ERR("SLP", "Unsupported BMP DIB header: %u", static_cast<unsigned>(dibSize));
    return false;
  }

  info.width = static_cast<int32_t>(readLE32(file));
  const auto rawHeight = static_cast<int32_t>(readLE32(file));
  if (rawHeight == std::numeric_limits<int32_t>::min()) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, rawHeight);
    return false;
  }
  info.topDown = rawHeight < 0;
  info.height = info.topDown ? -rawHeight : rawHeight;

  const uint16_t planes = readLE16(file);
  const uint16_t bpp = readLE16(file);
  const uint32_t compression = readLE32(file);

  // Match Bitmap::parseHeaders(): accept BI_RGB (0) and 32bpp BI_BITFIELDS (3), but keep the same
  // byte-layout assumption as custom sleep BMPs. The renderer below treats pixels as BGRA and does not parse masks.
  if (planes != 1 || bpp != 32 || !(compression == 0 || compression == 3)) {
    if (logErrors) {
      LOG_ERR("SLP", "Transparent overlay must be 32-bit BGRA BMP (planes=%u bpp=%u comp=%u)", planes, bpp,
              static_cast<unsigned>(compression));
    }
    return false;
  }

  constexpr int MAX_IMAGE_WIDTH = 2048;
  constexpr int MAX_IMAGE_HEIGHT = 3072;
  if (info.width <= 0 || info.height <= 0 || info.width > MAX_IMAGE_WIDTH || info.height > MAX_IMAGE_HEIGHT) {
    if (logErrors) LOG_ERR("SLP", "Bad transparent overlay dimensions: %dx%d", info.width, info.height);
    return false;
  }

  info.rowBytes = static_cast<uint32_t>(info.width) * 4u;
  if (!file.seek(info.dataOffset)) {
    if (logErrors) LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  return true;
}

uint8_t bayerThreshold4x4(const int x, const int y) {
  static constexpr uint8_t BAYER_4X4[16] = {0, 128, 32, 160, 192, 64, 224, 96, 48, 176, 16, 144, 240, 112, 208, 80};
  return BAYER_4X4[((y & 0x03) << 2) | (x & 0x03)];
}

enum class TransparentOverlayPass : uint8_t { BW, GrayscaleLsb, GrayscaleMsb };

uint8_t quantizeOverlayLum(const uint8_t lum) {
  // Match Bitmap's native-palette path: 0, 85, 170, 255 map directly to levels 0..3.
  return lum >> 6;
}

bool renderTransparentOverlayPass(HalFile& file, const OverlayBmpInfo& info, const BitmapPlacement& placement,
                                  const GfxRenderer& renderer, uint8_t* row, const TransparentOverlayPass pass) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return false;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cropPixX = std::floor(info.width * placement.cropX / 2.0f);
  const int cropPixY = std::floor(info.height * placement.cropY / 2.0f);
  const float croppedWidth = (1.0f - placement.cropX) * static_cast<float>(info.width);
  const float croppedHeight = (1.0f - placement.cropY) * static_cast<float>(info.height);

  float scale = 1.0f;
  if (croppedWidth > 0.0f && croppedHeight > 0.0f) {
    const float widthScale = static_cast<float>(pageWidth) / croppedWidth;
    const float heightScale = static_cast<float>(pageHeight) / croppedHeight;
    scale = std::min(widthScale, heightScale);
    if (scale > 1.0f) scale = 1.0f;
  }
  const bool isScaled = scale < 1.0f;

  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read in transparent overlay row %d", bmpY);
      return false;
    }

    int screenY = -cropPixY + (info.topDown ? bmpY : info.height - 1 - bmpY);
    if (isScaled) screenY = std::floor(screenY * scale);
    screenY += placement.y;

    if (screenY >= pageHeight) {
      if (info.topDown) break;
      continue;
    }
    if (screenY < 0) {
      if (!info.topDown) break;
      continue;
    }

    for (int bmpX = cropPixX; bmpX < info.width - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) screenX = std::floor(screenX * scale);
      screenX += placement.x;

      if (screenX >= renderer.getScreenWidth()) break;
      if (screenX < 0) continue;

      const uint8_t* pixel = row + (static_cast<size_t>(bmpX) * 4u);
      const uint8_t alpha = pixel[3];
      if (alpha < MIN_VISIBLE_ALPHA || alpha <= bayerThreshold4x4(screenX, screenY)) continue;

      const uint8_t lum = (77u * pixel[2] + 150u * pixel[1] + 29u * pixel[0]) >> 8;
      const uint8_t level = quantizeOverlayLum(lum);

      switch (pass) {
        case TransparentOverlayPass::BW:
          // Same first pass as custom bitmap sleep: all non-white levels are painted black.
          // Transparent overlay's only difference is that opaque white explicitly erases underlying text.
          renderer.drawPixel(screenX, screenY, level < 3);
          break;
        case TransparentOverlayPass::GrayscaleLsb:
        case TransparentOverlayPass::GrayscaleMsb: {
          const auto planePixel =
              grayPlanePixel(level, pass == TransparentOverlayPass::GrayscaleMsb, renderer.grayPlanesAreAbsolute());
          if (planePixel.write) renderer.drawPixel(screenX, screenY, planePixel.black);
          break;
        }
      }
    }
  }

  return true;
}

enum class AlphaOverlayResult : uint8_t { Rendered, NotAlphaOverlay, Error };
enum class AlphaScanResult : uint8_t { Useful, NotUseful, Error };

AlphaScanResult scanForUsefulAlpha(HalFile& file, const OverlayBmpInfo& info, uint8_t* row) {
  if (!file.seek(info.dataOffset)) {
    LOG_ERR("SLP", "Failed to seek transparent overlay pixel data");
    return AlphaScanResult::Error;
  }

  bool hasVisiblePixel = false;
  bool hasNonOpaquePixel = false;
  for (int bmpY = 0; bmpY < info.height; bmpY++) {
    if (file.read(row, info.rowBytes) != static_cast<int>(info.rowBytes)) {
      LOG_ERR("SLP", "Short read while checking transparent overlay row %d", bmpY);
      return AlphaScanResult::Error;
    }

    for (int bmpX = 0; bmpX < info.width; bmpX++) {
      const uint8_t alpha = row[static_cast<size_t>(bmpX) * 4u + 3u];
      hasVisiblePixel |= alpha >= MIN_VISIBLE_ALPHA;
      hasNonOpaquePixel |= alpha < 255;
      if (hasVisiblePixel && hasNonOpaquePixel) return AlphaScanResult::Useful;
    }
  }

  return AlphaScanResult::NotUseful;
}

AlphaOverlayResult tryRenderTransparentOverlayBmp(HalFile& file, GfxRenderer& renderer, const char* pathForLog) {
  OverlayBmpInfo info;
  if (!parseOverlayBmpHeader(file, info, false)) return AlphaOverlayResult::NotAlphaOverlay;

  const auto placement = calculateBitmapPlacement(info.width, info.height, renderer);
  auto row = makeUniqueNoThrow<uint8_t[]>(info.rowBytes);
  if (!row) {
    LOG_ERR("SLP", "OOM: transparent overlay row (%u bytes)", static_cast<unsigned>(info.rowBytes));
    return AlphaOverlayResult::Error;
  }

  const auto alphaScanResult = scanForUsefulAlpha(file, info, row.get());
  if (alphaScanResult == AlphaScanResult::Error) return AlphaOverlayResult::Error;
  if (alphaScanResult == AlphaScanResult::NotUseful) return AlphaOverlayResult::NotAlphaOverlay;

  LOG_DBG("SLP", "Rendering transparent overlay: %s (%dx%d)", pathForLog, info.width, info.height);

  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::BW))
    return AlphaOverlayResult::Error;
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  if (absolute) {
    cleanBeforeAbsoluteGray(renderer);
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return AlphaOverlayResult::Error;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleLsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    // Keep the current display instead of trying another overlay with a
    // framebuffer that now contains an incomplete gray plane.
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!renderTransparentOverlayPass(file, info, placement, renderer, row.get(), TransparentOverlayPass::GrayscaleMsb)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return AlphaOverlayResult::Rendered;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return AlphaOverlayResult::Rendered;
}

enum class SleepRecentKind : uint8_t { Standard, Overlay };

bool isRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx, const uint8_t window) {
  return recentKind == SleepRecentKind::Overlay ? APP_STATE.isRecentOverlaySleep(idx, window)
                                                : APP_STATE.isRecentSleep(idx, window);
}

void pushRecentSleepIndex(const SleepRecentKind recentKind, const uint16_t idx) {
  if (recentKind == SleepRecentKind::Overlay) {
    APP_STATE.pushRecentOverlaySleep(idx);
  } else {
    APP_STATE.pushRecentSleep(idx);
  }
}

bool findNextValidSleepImage(HalFile& dir, const SleepRecentKind recentKind, char* name) {
  for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
    if (dirFile.isDirectory()) continue;

    dirFile.getName(name, MAX_SLEEP_FILE_NAME_LEN);
    if (name[0] == '\0' || name[0] == '.') continue;

    const bool isBmp = FsHelpers::hasBmpExtension(name);
    const bool isPng = recentKind == SleepRecentKind::Overlay && FsHelpers::hasPngExtension(std::string_view{name});
    if (!isBmp && !isPng) {
      LOG_DBG("SLP", "Skipping unsupported sleep image: %s", name);
      continue;
    }

    const bool isValid = isBmp ? [&dirFile]() {
      Bitmap bitmap(dirFile);
      return bitmap.parseHeaders() == BmpReaderError::Ok;
    }()
                               : isValidPngHeader(dirFile);
    if (!isValid) {
      LOG_DBG("SLP", "Skipping invalid sleep image: %s", name);
      continue;
    }
    return true;
  }
  return false;
}

bool selectRandomSleepFile(const char* dirPath, const SleepRecentKind recentKind, std::string& selectedPath) {
  auto dir = Storage.open(dirPath);
  if (!dir || !dir.isDirectory()) return false;

  auto name = makeUniqueNoThrow<char[]>(MAX_SLEEP_FILE_NAME_LEN);
  if (!name) {
    LOG_ERR("SLP", "OOM: sleep filename buffer");
    return false;
  }

  uint16_t fileCount = 0;
  while (fileCount < UINT16_MAX && findNextValidSleepImage(dir, recentKind, name.get())) ++fileCount;
  if (fileCount == 0) return false;

  // Pick a random wallpaper, excluding recently shown ones.
  // Window: up to SLEEP_RECENT_COUNT entries, capped at fileCount-1.
  const uint8_t recentFill =
      recentKind == SleepRecentKind::Overlay ? APP_STATE.recentOverlaySleepFill : APP_STATE.recentSleepFill;
  const uint8_t window = static_cast<uint8_t>(std::min<uint16_t>(recentFill, fileCount - 1));
  auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
  for (uint8_t attempt = 0; attempt < 20 && isRecentSleepIndex(recentKind, randomFileIndex, window); attempt++) {
    randomFileIndex = static_cast<uint16_t>(random(fileCount));
  }

  dir.rewindDirectory();
  for (uint16_t index = 0; index <= randomFileIndex; ++index) {
    if (!findNextValidSleepImage(dir, recentKind, name.get())) return false;
  }

  selectedPath.reserve(strlen(dirPath) + 1 + strlen(name.get()));
  selectedPath = dirPath;
  selectedPath += "/";
  selectedPath += name.get();
  pushRecentSleepIndex(recentKind, randomFileIndex);
  APP_STATE.saveToFile();
  return true;
}

bool drawSleepPopupPreservingFrame(GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int frameThickness = metrics.popupFrameThickness;
  const int popupY = static_cast<int>(renderer.getScreenHeight() * metrics.popupTopOffsetRatio);
  const int popupHeight = renderer.getLineHeight(UI_12_FONT_ID) + metrics.popupMarginY * 2;
  const int bandTop = std::max(0, popupY - frameThickness);
  const int bandBottom = std::min(renderer.getScreenHeight(), popupY + popupHeight + frameThickness);
  const int bandHeight = bandBottom - bandTop;
  const size_t bandBytes = renderer.getRegionByteSize(0, bandTop, renderer.getScreenWidth(), bandHeight);

  auto savedBand = makeUniqueNoThrow<uint8_t[]>(bandBytes);
  if (!savedBand) {
    LOG_ERR("SLP", "OOM: sleep popup background (%u bytes)", static_cast<unsigned>(bandBytes));
    return false;
  }
  if (!renderer.copyRegionToBuffer(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to save sleep popup background");
    return false;
  }

  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  if (!renderer.copyBufferToRegion(0, bandTop, renderer.getScreenWidth(), bandHeight, savedBand.get(), bandBytes)) {
    LOG_ERR("SLP", "Failed to restore sleep popup background");
    return false;
  }
  return true;
}

void releaseSdFontCachesForDecode(const GfxRenderer& renderer) {
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("SLP", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("SLP", "Free heap before sleep image decode: %d bytes", ESP.getFreeHeap());
  }
}

}  // namespace

void SleepActivity::onExit() {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.endTextClip();
  Activity::onExit();
}

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    // Quick Resume keeps the current frame as-is, so the driver's inversion
    // state stays too: a night-mode page sleeps in night polarity, and the
    // moon icon inverts with it at transfer like any other draw.
    return renderLastScreenSleepScreen();
  }

  const bool frameWasInverted = display.isInverted();

  // The remaining sleep screens draw fresh content in normal polarity. This
  // activity draws directly from onEnter (outside ActivityManager's
  // per-render polarity resolution), so clear any inversion left over from a
  // night-mode reader render.
  display.setInverted(false);

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    // Transparent mode retains the current framebuffer. Materialize any
    // output-level inversion first so the retained content keeps its visible
    // polarity after the display driver returns to normal.
    if (frameWasInverted) renderer.invertScreen();
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    }
    drawSleepPopupPreservingFrame(renderer);
    if (APP_STATE.lastSleepFromReader) {
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    }
    releaseSdFontCachesForDecode(renderer);
    return renderTransparentCustomSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677 &&
                      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      return;
    }
    file.close();
  }

  std::string selectedPath;
  if (!selectRandomSleepFile("/.sleep", SleepRecentKind::Standard, selectedPath)) {
    selectRandomSleepFile("/sleep", SleepRecentKind::Standard, selectedPath);
  }

  if (!selectedPath.empty()) {
    HalFile randFile;
    if (Storage.openFileForRead("SLP", selectedPath, randFile)) {
      LOG_DBG("SLP", "Randomly loading: %s", selectedPath.c_str());
      delay(100);
      Bitmap bitmap(randFile, true,
                    renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported() &&
                        display.getController() == HalDisplay::Controller::SSD1677 &&
                        SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        randFile.close();
        return;
      }
      randFile.close();
    }
  }

  renderDefaultSleepScreen();
}

#include "ClockDigits.h"

namespace {
void drawDashedRoundedRect(const GfxRenderer& renderer, int x, int y, int w, int h, int r) {
  // Cạnh trên & cạnh dưới nét đứt
  for (int px = x + r; px < x + w - r; px += 3) {
    renderer.drawPixel(px, y, true);
    renderer.drawPixel(px, y + h - 1, true);
  }
  // Cạnh trái & cạnh phải nét đứt
  for (int py = y + r; py < y + h - r; py += 3) {
    renderer.drawPixel(x, py, true);
    renderer.drawPixel(x + w - 1, py, true);
  }
  // 4 góc bo nhẹ
  if (r > 0) {
    renderer.drawPixel(x + 1, y + 1, true);
    renderer.drawPixel(x + w - 2, y + 1, true);
    renderer.drawPixel(x + 1, y + h - 2, true);
    renderer.drawPixel(x + w - 2, y + h - 2, true);
  }
}

namespace {
bool isNonEmptyBmp(const std::string& path) {
  HalFile f;
  if (!Storage.openFileForRead("SLP", path, f)) return false;
  const size_t sz = f.size();
  f.close();
  return sz >= 54;
}
}  // namespace

std::string resolveRecentBookCoverPath(const RecentBook& book) {
  static constexpr int COMMON_HEIGHTS[] = {218, 226, 200, 180, 160, 234, 240, 248, 250, 260, 280, 320, 134, 151, 132, 300, 400};
  if (!book.coverBmpPath.empty()) {
    std::string path = book.coverBmpPath;
    const std::string placeholder = "[HEIGHT]";
    const size_t pos = path.find(placeholder);
    if (pos != std::string::npos) {
      for (const int h : COMMON_HEIGHTS) {
        std::string candidate = path;
        candidate.replace(pos, placeholder.length(), std::to_string(h));
        if (Storage.exists(candidate.c_str()) && isNonEmptyBmp(candidate)) return candidate;
      }
    } else if (Storage.exists(path.c_str()) && isNonEmptyBmp(path)) {
      return path;
    }
  }
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    for (const int h : COMMON_HEIGHTS) {
      const std::string thumb = epub.getThumbBmpPath(h);
      if (Storage.exists(thumb.c_str()) && isNonEmptyBmp(thumb)) return thumb;
    }
    const std::string cover = epub.getCoverBmpPath(false, false);
    if (Storage.exists(cover.c_str()) && isNonEmptyBmp(cover)) return cover;
    if (epub.generateThumbBmp(218)) {
      const std::string thumb = epub.getThumbBmpPath(218);
      if (Storage.exists(thumb.c_str()) && isNonEmptyBmp(thumb)) return thumb;
    }
  }
  if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    for (const int h : COMMON_HEIGHTS) {
      const std::string thumb = xtc.getThumbBmpPath(h);
      if (Storage.exists(thumb.c_str()) && isNonEmptyBmp(thumb)) return thumb;
    }
    const std::string cover = xtc.getCoverBmpPath();
    if (Storage.exists(cover.c_str()) && isNonEmptyBmp(cover)) return cover;
  }
  return "";
}
}  // namespace

void SleepActivity::renderDefaultSleepScreen(bool fast) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen(0xFF);

  // 1. TOP STATUS BAR (Không khung bo, thanh lịch, tinh tế)
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const unsigned clampedPin = percentage > 100 ? 100 : percentage;
  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "ĐÃ KHÓA  \u00b7  %u%%", clampedPin);
  renderer.drawCenteredText(UI_10_FONT_ID, 36, headerBuf, true);

  // 2. ĐỒNG HỒ SỐ LỚN VÀ NGÀY THÁNG (Ubuntu-Bold 74pt native bitmap, sắc nét 100% không rỗ)
  struct tm timeInfo{};
  char timeBuf[16] = "07:34";
  char dateBuf[48] = "Thứ Năm, 22/10";
  char lunarBuf[64] = "";

  if (halClock.isAvailable() && halClock.localTime(timeInfo)) {
    halClock.formatTime(timeBuf, sizeof(timeBuf), SETTINGS.clockFormat == 1);

    static const char* const kDayNamesVi[] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
    const char* dayVi = (timeInfo.tm_wday >= 0 && timeInfo.tm_wday < 7) ? kDayNamesVi[timeInfo.tm_wday] : "";
    snprintf(dateBuf, sizeof(dateBuf), "%s, %02d/%02d", dayVi, timeInfo.tm_mday, timeInfo.tm_mon + 1);

    const int solarYear = timeInfo.tm_year + 1900;
    const int solarMonth = timeInfo.tm_mon + 1;
    const int solarDay = timeInfo.tm_mday;
    LunarDate lunar = LunarCalendar::solarToLunar(solarDay, solarMonth, solarYear);
    if (lunar.isLeap) {
      snprintf(lunarBuf, sizeof(lunarBuf), "Âm lịch: %d/%d (Nhuận) · %s", lunar.day, lunar.month, lunar.yearCanChi.c_str());
    } else {
      snprintf(lunarBuf, sizeof(lunarBuf), "Âm lịch: %d/%d · %s", lunar.day, lunar.month, lunar.yearCanChi.c_str());
    }
  }

  // Đồng hồ số lớn Ubuntu TrueType native căn giữa (baselineY = 140)
  clock_font::drawClock(renderer, pageWidth / 2, 140, timeBuf);

  // Ngày tháng căn giữa thoáng đãng
  renderer.drawCenteredText(UI_10_FONT_ID, 162, dateBuf, true);

  // Lịch Âm căn giữa trang nhã bên dưới
  if (lunarBuf[0] != '\0') {
    renderer.drawCenteredText(UI_10_FONT_ID, 188, lunarBuf, true);
  }

  // 3. 2 THẺ THÔNG TIN ĐƯỢC CHUYỂN XUỐNG DƯỚI (Neo-Brutalist Cards)
  constexpr int cardX = 24;
  const int cardW = pageWidth - cardX * 2;
  constexpr int card1H = 164;
  constexpr int card2H = 176;

  // Thẻ 2 neo ở đáy (cách mép dưới 36px)
  const int card2Y = pageHeight - card2H - 36; // 800 - 176 - 36 = 588px
  // Thẻ 1 cách thẻ 2 16px
  const int card1Y = card2Y - card1H - 16;      // 588 - 164 - 16 = 408px

  // Khung bo góc viền đơn mảnh thẻ 1
  renderer.drawRoundedRect(cardX, card1Y, cardW, card1H, 1, 14, true);

  const auto& books = RECENT_BOOKS.getBooks();
  const RecentBook* book = nullptr;
  if (!APP_STATE.openEpubPath.empty()) {
    for (const auto& b : books) {
      if (b.path == APP_STATE.openEpubPath) {
        book = &b;
        break;
      }
    }
  }
  if (!book && !books.empty()) {
    book = &books.front();
  }

  // Bìa sách bên trái
  constexpr int coverW = 86;
  constexpr int coverH = 132;
  constexpr int coverX = cardX + 16;
  const int coverY = card1Y + 16;
  renderer.drawRoundedRect(coverX, coverY, coverW, coverH, 1, 6, true);

  bool drewCoverImage = false;
  if (book != nullptr) {
    const std::string coverPath = resolveRecentBookCoverPath(*book);
    if (!coverPath.empty()) {
      HalFile file;
      if (Storage.openFileForRead("SLP", coverPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderer.drawBitmap(bitmap, coverX + 1, coverY + 1, coverW - 2, coverH - 2);
          drewCoverImage = true;
        }
        file.close();
      }
    }

    if (!drewCoverImage) {
      // Texture giả lập bìa sách đổ hạt tinh tế
      for (int py = coverY + 4; py < coverY + coverH - 4; py += 3) {
        renderer.drawLine(coverX + 6, py, coverX + coverW - 6, py, 1, true);
      }
    }

    // Text thông tin sách bên phải
    constexpr int textX = coverX + coverW + 18;
    const int maxTextW = cardW - (textX - cardX) - 16;

    // Tên sách
    const std::string titleStr = renderer.truncatedText(UI_12_FONT_ID, book->title.empty() ? book->path.c_str() : book->title.c_str(), maxTextW);
    renderer.drawText(UI_12_FONT_ID, textX, card1Y + 24, titleStr.c_str(), true, EpdFontFamily::BOLD);

    // Tác giả
    const std::string authStr = renderer.truncatedText(UI_10_FONT_ID, book->author.empty() ? "CrossPoint" : book->author.c_str(), maxTextW);
    renderer.drawText(UI_10_FONT_ID, textX, card1Y + 66, authStr.c_str(), true);

    // Tiến độ đọc & thời gian đọc
    const int progress = loadBookProgress(book->path);
    const int readPct = progress >= 0 ? progress : 0;
    char readStatsBuf[64];
    snprintf(readStatsBuf, sizeof(readStatsBuf), "\u0110\u00e3 \u0111\u1ecdc: %d%% \u00b7 5 ph\u00fat", readPct);
    renderer.drawText(UI_10_FONT_ID, textX, card1Y + 104, readStatsBuf, true);

    // Thanh gạch ngang mảnh tiến độ
    const int barY = card1Y + 132;
    const int barEndX = cardX + cardW - 18;
    renderer.drawLine(textX, barY, barEndX, barY, 1, true);
    if (readPct > 0) {
      const int filledW = std::clamp((barEndX - textX) * readPct / 100, 2, barEndX - textX);
      renderer.drawLine(textX, barY, textX + filledW, barY, 2, true);
    }
  } else {
    // Khi chưa mở sách nào
    for (int py = coverY + 4; py < coverY + coverH - 4; py += 3) {
      renderer.drawLine(coverX + 6, py, coverX + coverW - 6, py, 1, true);
    }
    constexpr int textX = coverX + coverW + 18;
    renderer.drawText(UI_12_FONT_ID, textX, card1Y + 36, "CROSSBASIC OS", true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, textX, card1Y + 74, "Kh\u00f4ng c\u00f3 s\u00e1ch \u0111ang \u0111\u1ecdc", true);
    renderer.drawText(UI_10_FONT_ID, textX, card1Y + 110, "M\u1edf th\u01b0 vi\u1ec7n \u0111\u1ec3 b\u1eaft \u0111\u1ea7u", true);
  }

  // 4. THẺ 2: THỐNG KÊ THÁNG NÀY & LƯỚI LỊCH HEATMAP (READING STATS & HEATMAP)
  renderer.drawRoundedRect(cardX, card2Y, cardW, card2H, 1, 14, true);

  // Nạp thống kê đọc sách toàn hệ thống
  const GlobalReadingStats globalStats = GlobalReadingStats::load();
  ReadingStatsDateTime statsNow{};
  getCurrentLocalReadingStatsDateTime(statsNow);

  // Cột bên trái: Thống kê tổng quan
  constexpr int statX = cardX + 20;
  renderer.drawText(UI_10_FONT_ID, statX, card2Y + 18, "Th\u00e1ng n\u00e0y", true);

  // Tổng thời gian đọc số to
  char durBuf[32] = "7m";
  if (globalStats.totalReadingSeconds > 0) {
    formatCompactReadingDuration(globalStats.totalReadingSeconds, durBuf, sizeof(durBuf));
  }
  renderer.drawText(NOTOSANS_18_FONT_ID, statX, card2Y + 42, durBuf, true, EpdFontFamily::BOLD);

  // Số sách hoàn thành
  char finishedBuf[32];
  snprintf(finishedBuf, sizeof(finishedBuf), "%u cu\u1ed1n xong", static_cast<unsigned>(globalStats.completedBooks));
  renderer.drawText(UI_10_FONT_ID, statX, card2Y + 86, finishedBuf, true);

  // ĐỔI DẠNG CHUỖI ĐỌC: Khung viền đơn mảnh bo góc, căn giữa chữ chuẩn mực, bỏ hoàn toàn icon chấm/ngọn lửa
  const uint16_t streak = globalStats.currentReadingStreak(&statsNow.date);
  const int badgeY = card2Y + 118;
  char streakText[32];
  snprintf(streakText, sizeof(streakText), "Chu\u1ed7i %u ng\u00e0y", static_cast<unsigned>(streak > 0 ? streak : 1));
  const int streakW = renderer.getTextWidth(UI_10_FONT_ID, streakText, EpdFontFamily::BOLD);
  const int badgeW = streakW + 24;
  constexpr int badgeH = 26;

  // Viền đơn mảnh bo góc 8px, nền trắng thanh nhã
  renderer.drawRoundedRect(statX, badgeY, badgeW, badgeH, 1, 8, true);

  // Chữ căn giữa chuẩn mực bên trong khung
  const int textY = badgeY + (badgeH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  renderer.drawText(UI_10_FONT_ID, statX + 12, textY, streakText, true, EpdFontFamily::BOLD);

  // Lưới Lịch Tháng (Calendar Heatmap Grid 7 cột)
  constexpr int cellSize = 15;
  constexpr int cellGap = 4;
  constexpr int gridCols = 7;
  constexpr int gridW = gridCols * cellSize + (gridCols - 1) * cellGap; // 129px
  const int gridX = cardX + cardW - gridW - 20;
  const int gridY = card2Y + 22;

  const uint16_t curYear = (timeInfo.tm_year > 0) ? (timeInfo.tm_year + 1900) : 2026;
  const uint8_t curMonth = (timeInfo.tm_mon >= 0 && timeInfo.tm_mon < 12) ? (timeInfo.tm_mon + 1) : 10;
  const uint8_t curDay = (timeInfo.tm_mday > 0 && timeInfo.tm_mday <= 31) ? timeInfo.tm_mday : 22;
  const uint8_t totalDaysInMonth = daysInMonth(curYear, curMonth);

  // Thứ của ngày mùng 1 (Quy ước: Thứ Hai = 0 ... Chủ Nhật = 6)
  int firstWday = (timeInfo.tm_wday - ((curDay - 1) % 7) + 7) % 7;
  int startCol = (firstWday + 6) % 7; // tm_wday: 0=CN, 1=T2 -> đổi thành T2=0, CN=6

  for (uint8_t d = 1; d <= totalDaysInMonth; ++d) {
    const int slot = startCol + (d - 1);
    const int col = slot % 7;
    const int row = slot / 7;

    const int cx = gridX + col * (cellSize + cellGap);
    const int cy = gridY + row * (cellSize + cellGap);

    if (d <= curDay) {
      // Ngày quá khứ hoặc hôm nay
      bool hasRead = (d == curDay); // Hôm nay mặc định active
      if (globalStats.readingHistoryAnchorDay > 0) {
        const ReadingStatsDate checkDate{curYear, curMonth, d};
        const uint32_t checkIndex = readingStatsDayIndex(checkDate);
        if (checkIndex <= globalStats.readingHistoryAnchorDay) {
          const uint32_t delta = globalStats.readingHistoryAnchorDay - checkIndex;
          if (delta < READING_HISTORY_DAYS) {
            hasRead = ((globalStats.readingHistoryBits[delta / 8] >> (delta % 8)) & 1) != 0;
            if (d == curDay) hasRead = true;
          }
        }
      }

      if (hasRead) {
        renderer.fillRoundedRect(cx, cy, cellSize, cellSize, 2, Color::Black);
      } else {
        renderer.drawRoundedRect(cx, cy, cellSize, cellSize, 1, 2, true);
      }
    } else {
      // Các ngày trong tương lai: vẽ viền nét đứt (dashed)
      drawDashedRoundedRect(renderer, cx, cy, cellSize, cellSize, 2);
    }
  }

  // Đảo màu thành nền tối nếu người dùng chọn chế độ "Đồng hồ (Tối)"
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::DARK) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(fast ? HalDisplay::FAST_REFRESH : kSleepClean);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preserveBackground) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  if (!preserveBackground) renderer.clearScreen();

  const bool hasGreyscale =
      bitmap.hasGreyscale() && (preserveBackground || SETTINGS.sleepScreenCoverFilter ==
                                                          CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);

  if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
    renderer.displayBuffer(kSleepClean);
    return;
  }

  if (!preserveBackground &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  const bool absolute = hasGreyscale && renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  if (absolute) {
    cleanBeforeAbsoluteGray(renderer);
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return;
  } else if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(kSleepClean);
  }

  if (hasGreyscale) {
    bool ready = true;
    for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
      if (bitmap.rewindToData() != BmpReaderError::Ok) {
        ready = false;
        break;
      }
      if (!absolute || !preserveBackground) renderer.clearScreen(absolute ? 0xFF : 0x00);
      renderer.setRenderMode(plane);
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, preserveBackground)) {
        ready = false;
        break;
      }
      if (plane == GfxRenderer::GRAYSCALE_LSB)
        renderer.copyGrayscaleLsbBuffers();
      else
        renderer.copyGrayscaleMsbBuffers();
    }
    if (ready)
      renderer.displayGrayBuffer();
    else
      LOG_ERR("SLP", "Incomplete grayscale image; keeping the current display");
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

bool SleepActivity::renderSleepOverlayFile(HalFile& file, const char* pathForLog) const {
  const auto alphaResult = tryRenderTransparentOverlayBmp(file, renderer, pathForLog);
  if (alphaResult == AlphaOverlayResult::Rendered) return true;
  if (alphaResult == AlphaOverlayResult::Error) return false;

  Bitmap bitmap(file);
  const auto parseResult = bitmap.parseHeaders();
  if (parseResult != BmpReaderError::Ok) {
    LOG_ERR("SLP", "Invalid sleep overlay BMP %s: %s", pathForLog, Bitmap::errorToString(parseResult));
    return false;
  }

  LOG_DBG("SLP", "Rendering regular BMP sleep overlay: %s (%dx%d)", pathForLog, bitmap.getWidth(), bitmap.getHeight());
  // drawBitmap leaves white pixels untouched; skipping the initial clear makes
  // them transparent while retaining the existing grayscale pipeline.
  renderBitmapSleepScreen(bitmap, true);
  return true;
}

bool SleepActivity::renderTransparentOverlayPng(const std::string& path) const {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(path, dimensions)) return false;

  const auto placement = calculateBitmapPlacement(dimensions.width, dimensions.height, renderer);
  RenderConfig config;
  config.x = placement.x;
  config.y = placement.y;
  config.maxWidth = renderer.getScreenWidth();
  config.maxHeight = renderer.getScreenHeight();
  config.useDithering = false;
  config.sourceCropX = placement.cropX;
  config.sourceCropY = placement.cropY;
  config.useExactDimensions = placement.cropX > 0.0f || placement.cropY > 0.0f;
  config.preserveAlpha = true;

  PngToFramebufferConverter converter;
  LOG_DBG("SLP", "Rendering transparent PNG overlay: %s (%dx%d)", path.c_str(), dimensions.width, dimensions.height);

  if (!converter.decodeToFramebuffer(path, renderer, config)) return false;
  const bool absolute = renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported();
  if (absolute) {
    if (!renderer.displayGrayscaleBase(sleepGrayscaleMode(renderer))) return false;
  } else {
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  }

  // Absolute planes retain B/W background bits; each visible overlay pixel is rewritten in both passes.
  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleLsbBuffers();

  if (!absolute) renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!converter.decodeToFramebuffer(path, renderer, config)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return true;
  }
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool SleepActivity::renderSleepOverlayPath(const std::string& path) const {
  if (FsHelpers::hasPngExtension(path)) {
    return Storage.exists(path.c_str()) && renderTransparentOverlayPng(path);
  }

  HalFile file;
  return Storage.openFileForRead("SLP", path, file) && renderSleepOverlayFile(file, path.c_str());
}

void SleepActivity::renderTransparentCustomSleepScreen() const {
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_BMP)) return;
  if (renderSleepOverlayPath(TRANSPARENT_SLEEP_ROOT_PNG)) return;

  std::string selectedPath;
  if (!selectRandomSleepFile(TRANSPARENT_SLEEP_DIR, SleepRecentKind::Overlay, selectedPath)) {
    selectRandomSleepFile(TRANSPARENT_SLEEP_LEGACY_DIR, SleepRecentKind::Overlay, selectedPath);
  }

  if (!selectedPath.empty() && renderSleepOverlayPath(selectedPath)) return;

  LOG_ERR("SLP", "No valid transparent sleep overlay found");
  renderDefaultSleepScreen();
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  // 1. Xác định đường dẫn cuốn sách cần hiện bìa
  std::string targetBookPath = APP_STATE.openEpubPath;
  const auto& books = RECENT_BOOKS.getBooks();
  const RecentBook* matchedBook = nullptr;

  if (!targetBookPath.empty()) {
    for (const auto& b : books) {
      if (b.path == targetBookPath) {
        matchedBook = &b;
        break;
      }
    }
  }

  if (targetBookPath.empty()) {
    if (!books.empty()) {
      matchedBook = &books.front();
      targetBookPath = matchedBook->path;
    }
  }

  // Nếu trong máy hoàn toàn chưa từng mở hoặc có sách nào
  if (targetBookPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  // SSD absolute images use the new thresholds; other panels retain legacy tuning.
  const bool originalThresholds =
      renderer.grayscaleCapabilities(sleepGrayscaleMode(renderer)).supported() &&
      display.getController() == HalDisplay::Controller::SSD1677 &&
      SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // 2. Thử tạo và tìm ảnh bìa full màn hình
  if (FsHelpers::hasXtcExtension(targetBookPath)) {
    Xtc lastXtc(targetBookPath, "/.crosspoint");
    if (lastXtc.load() && lastXtc.generateCoverBmp()) {
      coverBmpPath = lastXtc.getCoverBmpPath();
    }
  } else if (FsHelpers::hasReflowableBookExtension(targetBookPath)) {
    Epub lastEpub(targetBookPath, "/.crosspoint");
    if (lastEpub.load(true, true) && lastEpub.generateCoverBmp(cropped, originalThresholds)) {
      coverBmpPath = lastEpub.getCoverBmpPath(cropped, originalThresholds);
    }
  }

  // 3. Nếu có file cover full, mở và hiển thị tràn màn hình
  if (!coverBmpPath.empty()) {
    HalFile file;
    if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        LOG_DBG("SLP", "Rendering full sleep cover: %s", coverBmpPath.c_str());
        renderBitmapSleepScreen(bitmap);
        file.close();
        return;
      }
      file.close();
    }
  }

  // 4. Nếu không có cover full, thử tìm thumbnail đã cache của sách
  std::string thumbCoverPath;
  if (matchedBook != nullptr) {
    thumbCoverPath = resolveRecentBookCoverPath(*matchedBook);
  } else {
    RecentBook tempBook{};
    tempBook.path = targetBookPath;
    thumbCoverPath = resolveRecentBookCoverPath(tempBook);
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Đọc thông tin tên sách và tác giả
  std::string bookTitle = matchedBook ? matchedBook->title : "";
  std::string bookAuthor = matchedBook ? matchedBook->author : "";
  if (bookTitle.empty()) {
    bookTitle = FsHelpers::getFileNameWithoutExtension(targetBookPath);
  }
  const int progress = loadBookProgress(targetBookPath);
  const int readPct = progress >= 0 ? progress : 0;

  // 5. Nếu có ảnh thumbnail, vẽ màn hình bìa sách nghệ thuật (Artistic Cover)
  if (!thumbCoverPath.empty()) {
    HalFile thumbFile;
    if (Storage.openFileForRead("SLP", thumbCoverPath, thumbFile)) {
      Bitmap bitmap(thumbFile);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.clearScreen(0xFF);

        // Header nhỏ
        const uint16_t percentage = powerManager.getBatteryPercentage();
        const unsigned clampedPin = percentage > 100 ? 100 : percentage;
        char headerBuf[32];
        snprintf(headerBuf, sizeof(headerBuf), "ĐÃ KHÓA  ·  %u%%", clampedPin);
        renderer.drawCenteredText(UI_10_FONT_ID, 36, headerBuf, true);

        // Bìa sách căn giữa với bóng đổ tinh tế
        constexpr int coverW = 240;
        constexpr int coverH = 360;
        const int coverX = (pageWidth - coverW) / 2;
        constexpr int coverY = 88;

        renderer.fillRoundedRect(coverX + 4, coverY + 4, coverW, coverH, 10, Color::Black);
        renderer.fillRoundedRect(coverX, coverY, coverW, coverH, 10, Color::White);
        renderer.drawBitmap(bitmap, coverX, coverY, coverW, coverH);
        renderer.drawRoundedRect(coverX, coverY, coverW, coverH, 2, 10, true);

        // Thông tin sách phía dưới
        const int textMaxW = pageWidth - 64;
        const std::string truncTitle = renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), textMaxW);
        renderer.drawCenteredText(UI_12_FONT_ID, 490, truncTitle.c_str(), true, EpdFontFamily::BOLD);

        if (!bookAuthor.empty()) {
          const std::string truncAuth = renderer.truncatedText(UI_10_FONT_ID, bookAuthor.c_str(), textMaxW);
          renderer.drawCenteredText(UI_10_FONT_ID, 534, truncAuth.c_str(), true);
        }

        char progBuf[48];
        snprintf(progBuf, sizeof(progBuf), "Tiến độ đọc: %d%%", readPct);
        renderer.drawCenteredText(UI_10_FONT_ID, 574, progBuf, true);

        // Thanh tiến độ thanh mảnh
        constexpr int barW = 200;
        const int barX = (pageWidth - barW) / 2;
        constexpr int barY = 612;
        renderer.drawLine(barX, barY, barX + barW, barY, 1, true);
        if (readPct > 0) {
          const int fillW = std::clamp((barW * readPct) / 100, 2, barW);
          renderer.drawLine(barX, barY, barX + fillW, barY, 3, true);
        }

        thumbFile.close();
        renderer.displayBuffer(kSleepClean);
        return;
      }
      thumbFile.close();
    }
  }

  // 6. Nếu không có bất kỳ ảnh bìa nào, vẽ Bìa Typographic thanh lịch sang trọng
  renderer.clearScreen(0xFF);

  // Khung viền sách cổ điển
  constexpr int frameMargin = 32;
  renderer.drawRoundedRect(frameMargin, frameMargin, pageWidth - frameMargin * 2, pageHeight - frameMargin * 2, 2, 16, true);
  renderer.drawRoundedRect(frameMargin + 8, frameMargin + 8, pageWidth - (frameMargin + 8) * 2, pageHeight - (frameMargin + 8) * 2, 1, 12, true);

  // Header pin
  const uint16_t percentage = powerManager.getBatteryPercentage();
  const unsigned clampedPin = percentage > 100 ? 100 : percentage;
  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "CROSSPOINT READER  ·  %u%%", clampedPin);
  renderer.drawCenteredText(UI_10_FONT_ID, 70, headerBuf, true);

  // Tên sách nổi bật ở trung tâm
  const int maxW = pageWidth - 100;
  const std::string titleLine = renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), maxW);
  renderer.drawCenteredText(UI_12_FONT_ID, 280, titleLine.c_str(), true, EpdFontFamily::BOLD);

  if (!bookAuthor.empty()) {
    const std::string authLine = renderer.truncatedText(UI_12_FONT_ID, bookAuthor.c_str(), maxW);
    renderer.drawCenteredText(UI_12_FONT_ID, 340, authLine.c_str(), true);
  }

  char progBuf[48];
  snprintf(progBuf, sizeof(progBuf), "Tiến độ: %d%%", readPct);
  renderer.drawCenteredText(UI_10_FONT_ID, 430, progBuf, true);

  constexpr int barW = 180;
  const int barX = (pageWidth - barW) / 2;
  constexpr int barY = 465;
  renderer.drawLine(barX, barY, barX + barW, barY, 1, true);
  if (readPct > 0) {
    const int fillW = std::clamp((barW * readPct) / 100, 2, barW);
    renderer.drawLine(barX, barY, barX + fillW, barY, 3, true);
  }

  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 75, "Đang ở chế độ ngủ (Chạm để tiếp tục)", true);
  renderer.displayBuffer(kSleepClean);
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  // Only the moon differs from the displayed frame, so a differential FAST
  // update adds it without the flashing clean pass (which sweeps the panel
  // through the inverse — a full white flash on a night-mode page).
  if (gpio.deviceIsX3()) {
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(kSleepClean);
}
