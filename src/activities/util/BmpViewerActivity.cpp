#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/JpegToFramebufferConverter.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr char CUSTOM_SLEEP_ROOT_BMP[] = "/sleep.bmp";
constexpr char CUSTOM_SLEEP_TMP_BMP[] = "/sleep.bmp.tmp";
constexpr char JPEG_SLEEP_TMP_PXC[] = "/sleep.bmp.pxc.tmp";
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
constexpr size_t COPY_BUFFER_SIZE = 2048;
constexpr uint8_t GRAYSCALE_PALETTE[] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};

RenderConfig fitImage(const ImageDimensions& dimensions, const GfxRenderer& renderer) {
  const float scale = std::min({static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                                static_cast<float>(renderer.getScreenHeight()) / dimensions.height, 1.0f});
  const int width = std::max(1, std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * scale)));
  const int height = std::max(1, std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * scale)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};
  config.useExactDimensions = true;
  return config;
}
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                     bool returnToCaller)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)), returnToCaller(returnToCaller) {}

void BmpViewerActivity::loadSiblingImages() {
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  const auto name = makeUniqueNoThrow<char[]>(500);
  if (!name) {
    LOG_ERR("BMP", "OOM: sibling filename buffer");
    return;
  }
  size_t imageCount = 0;
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(name.get(), 500);
    if (name[0] != '.' && FsHelpers::hasImageExtension(std::string_view{name.get()})) ++imageCount;
  }
  dir.rewindDirectory();
  siblingImages.reserve(imageCount);

  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name.get(), 500);
      if (name[0] != '.') {
        std::string fname(name.get());
        if (FsHelpers::hasImageExtension(fname)) {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  const auto image = std::find(siblingImages.begin(), siblingImages.end(), fileName);
  if (image != siblingImages.end()) {
    currentImageIndex = static_cast<int>(image - siblingImages.begin());
  }
}

bool BmpViewerActivity::canSetSleepCover(const std::string& path) {
  return FsHelpers::hasBmpExtension(path) || FsHelpers::hasJpgExtension(path) || FsHelpers::hasPngExtension(path);
}

bool BmpViewerActivity::canSetSleepCover() const {
  return canSetSleepCover(filePath);
}

bool BmpViewerActivity::renderImage() {
  const bool jpeg = FsHelpers::hasJpgExtension(filePath);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      jpeg ? static_cast<ImageToFramebufferDecoder&>(jpegDecoder) : static_cast<ImageToFramebufferDecoder&>(pngDecoder);
  ImageDimensions dimensions;
  if (!decoder.getDimensions(filePath, dimensions)) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  const auto config = fitImage(dimensions, renderer);
  const bool hasPrevious = siblingImages.size() > 1 && currentImageIndex > 0;
  const bool hasNext = siblingImages.size() > 1 && currentImageIndex != -1 &&
                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "",
                                            hasPrevious ? "<" : "", hasNext ? ">" : "");
  const auto drawImage = [&]() {
    if (!decoder.decodeToFramebuffer(filePath, renderer, config)) return false;
    if (!mappedInput.hasTouch()) {
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else if (controlsVisible) {
      drawSquircleButtons();
    }
    return true;
  };

  if (!drawImage()) return false;
  if (!jpeg) {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return true;
  }

  const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
  if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return false;
  if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  bool ready = true;
  // ponytail: decode each plane; add a pixel cache if large-image latency needs it.
  for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(absolute ? 0xFF : 0x00);
    renderer.setRenderMode(mode);
    if (!drawImage()) {
      ready = false;
      break;
    }
    if (mode == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  if (ready) renderer.displayGrayBuffer();

  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen();
  if (!drawImage()) ready = false;
  renderer.cleanupGrayscaleWithFrameBuffer();
  return ready;
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();
  controlsShownMillis = millis();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasPngExtension(filePath) || FsHelpers::hasJpgExtension(filePath)) {
    renderer.clearScreen();
    if (!renderImage()) {
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, "", "", "");
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }

  HalFile file;
  // 1. Open the BMP file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true,
                  renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported() &&
                      display.getController() == HalDisplay::Controller::SSD1677);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                      currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      const auto labels = mappedInput.mapLabels(tr(STR_BACK), canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "",
                                                (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
        renderer.clearScreen();
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }

      // Draw UI hints on the base layer
      if (!mappedInput.hasTouch()) {
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      } else if (controlsVisible) {
        drawSquircleButtons();
      }
      if (bitmap.hasGreyscale()) {
        const bool absolute = renderer.grayscaleCapabilities(HalDisplay::GrayscaleMode::Absolute).supported();
        if (absolute && !renderer.displayGrayscaleBase(HalDisplay::GrayscaleMode::Absolute)) return;
        if (!absolute) renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
        bool planesReady = true;
        for (const auto mode : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
          if (bitmap.rewindToData() != BmpReaderError::Ok) {
            LOG_ERR("BMP", "Failed to rewind bitmap for grayscale rendering");
            planesReady = false;
            break;
          }
          renderer.clearScreen(absolute ? 0xFF : 0x00);
          renderer.setRenderMode(mode);
          if (!renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
            planesReady = false;
            break;
          }
          if (!mappedInput.hasTouch()) {
            GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
          } else if (controlsVisible) {
            drawSquircleButtons();
          }
          if (mode == GfxRenderer::GRAYSCALE_LSB) {
            renderer.copyGrayscaleLsbBuffers();
          } else {
            renderer.copyGrayscaleMsbBuffers();
          }
        }
        if (planesReady) renderer.displayGrayBuffer();

        // Rebuild the BW framebuffer for popups and subsequent differential updates.
        renderer.setRenderMode(GfxRenderer::BW);
        renderer.clearScreen();
        if (bitmap.rewindToData() != BmpReaderError::Ok ||
            !renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0)) {
          LOG_ERR("BMP", "Failed to rewind bitmap to restore the BW framebuffer");
          renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
          planesReady = false;
        }
        if (!mappedInput.hasTouch()) {
          GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        } else if (controlsVisible) {
          drawSquircleButtons();
        }
        renderer.cleanupGrayscaleWithFrameBuffer();
        if (!planesReady) renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      }

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool BmpViewerActivity::saveImageSleepCover(GfxRenderer& renderer, const std::string& path) {
  if (Storage.exists(JPEG_SLEEP_TMP_PXC) && !Storage.remove(JPEG_SLEEP_TMP_PXC)) {
    LOG_ERR("BMP", "Failed to remove previous sleep image pixels");
    return false;
  }
  bool keepBmpTemp = false;
  const ScopedCleanup cleanup{[&keepBmpTemp]() {
    Storage.remove(JPEG_SLEEP_TMP_PXC);
    if (!keepBmpTemp) Storage.remove(CUSTOM_SLEEP_TMP_BMP);
  }};

  const bool isPng = FsHelpers::hasPngExtension(path);
  JpegToFramebufferConverter jpegDecoder;
  PngToFramebufferConverter pngDecoder;
  ImageToFramebufferDecoder& decoder =
      isPng ? static_cast<ImageToFramebufferDecoder&>(pngDecoder) : static_cast<ImageToFramebufferDecoder&>(jpegDecoder);

  ImageDimensions dimensions;
  if (!decoder.getDimensions(path, dimensions)) return false;
  auto config = fitImage(dimensions, renderer);
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP &&
      (dimensions.width > renderer.getScreenWidth() || dimensions.height > renderer.getScreenHeight())) {
    const float ratio = static_cast<float>(dimensions.width) / dimensions.height;
    const float screenRatio = static_cast<float>(renderer.getScreenWidth()) / renderer.getScreenHeight();
    config.x = config.y = 0;
    config.maxWidth = renderer.getScreenWidth();
    config.maxHeight = renderer.getScreenHeight();
    if (ratio > screenRatio) {
      config.sourceCropX = 1.0f - screenRatio / ratio;
    } else {
      config.sourceCropY = 1.0f - ratio / screenRatio;
    }
  }
  config.cachePath = JPEG_SLEEP_TMP_PXC;
  renderer.clearScreen();
  bool cacheWritten = false;
  if (isPng) {
    if (!pngDecoder.decodeToFramebuffer(path, renderer, config)) {
      LOG_ERR("BMP", "Failed to decode PNG sleep image");
      return false;
    }
  } else {
    if (!jpegDecoder.decodeToFramebuffer(path, renderer, config, cacheWritten) || !cacheWritten) {
      LOG_ERR("BMP", "Failed to decode JPEG sleep image");
      return false;
    }
  }

  HalFile input, output;
  if (!Storage.openFileForRead("BMP", JPEG_SLEEP_TMP_PXC, input)) return false;
  uint16_t width, height;
  if (input.read(&width, sizeof(width)) != sizeof(width) || input.read(&height, sizeof(height)) != sizeof(height) ||
      width != config.maxWidth || height != config.maxHeight) {
    LOG_ERR("BMP", "Invalid sleep image pixels");
    return false;
  }
  const size_t pixelRowBytes = (width + 3) / 4;
  const size_t bmpRowBytes = (width + 15) / 16 * 4;
  if (input.size() != 4 + pixelRowBytes * height) {
    LOG_ERR("BMP", "Incomplete sleep image pixels");
    return false;
  }
  auto row = makeUniqueNoThrow<uint8_t[]>(bmpRowBytes);
  if (!row) {
    LOG_ERR("BMP", "OOM: sleep image row");
    return false;
  }
  if (!Storage.openFileForWrite("BMP", CUSTOM_SLEEP_TMP_BMP, output)) return false;
  BmpHeader header;
  createBmpHeader(&header, width, height, BmpRowOrder::TopDown);
  header.infoHeader.biBitCount = 2;
  header.infoHeader.biClrUsed = header.infoHeader.biClrImportant = 4;
  header.infoHeader.biSizeImage = bmpRowBytes * height;
  header.fileHeader.bfOffBits = sizeof(header) - sizeof(header.colors) + sizeof(GRAYSCALE_PALETTE);
  header.fileHeader.bfSize = header.fileHeader.bfOffBits + header.infoHeader.biSizeImage;
  if (output.write(&header, sizeof(header) - sizeof(header.colors)) != sizeof(header) - sizeof(header.colors) ||
      output.write(GRAYSCALE_PALETTE, sizeof(GRAYSCALE_PALETTE)) != sizeof(GRAYSCALE_PALETTE)) {
    LOG_ERR("BMP", "Failed to write sleep image header");
    return false;
  }
  for (int y = 0; y < height; ++y) {
    if (input.read(row.get(), pixelRowBytes) != pixelRowBytes || output.write(row.get(), bmpRowBytes) != bmpRowBytes) {
      LOG_ERR("BMP", "Failed to write sleep image row %d", y);
      return false;
    }
  }
  if (!output.close()) {
    LOG_ERR("BMP", "Failed to close sleep image");
    return false;
  }
  if (!Storage.replaceFile(CUSTOM_SLEEP_TMP_BMP, CUSTOM_SLEEP_ROOT_BMP)) {
    keepBmpTemp = true;
    LOG_ERR("BMP", "Failed to publish sleep image");
    return false;
  }
  return true;
}

bool BmpViewerActivity::saveJpegSleepCover() {
  return saveImageSleepCover(renderer, filePath);
}

bool BmpViewerActivity::setSleepCoverFromFile(GfxRenderer& renderer, const std::string& path) {
  if (!canSetSleepCover(path)) return false;

  const bool isImage = FsHelpers::hasJpgExtension(path) || FsHelpers::hasPngExtension(path);
  bool success = false;

  if (isImage) {
    success = saveImageSleepCover(renderer, path);
  } else if (FsHelpers::hasBmpExtension(path)) {
    if (path == CUSTOM_SLEEP_ROOT_BMP) {
      success = true;
    } else {
      auto buffer = makeUniqueNoThrow<uint8_t[]>(COPY_BUFFER_SIZE);
      if (buffer) {
        const std::string tmp = std::string(CUSTOM_SLEEP_ROOT_BMP) + ".tmp";
        HalFile inFile, outFile;
        if (Storage.openFileForRead("BMP", path, inFile) && Storage.openFileForWrite("BMP", tmp, outFile)) {
          int bytesRead;
          success = true;
          while ((bytesRead = inFile.read(buffer.get(), COPY_BUFFER_SIZE)) > 0) {
            if (outFile.write(buffer.get(), static_cast<size_t>(bytesRead)) != static_cast<size_t>(bytesRead)) {
              success = false;
              break;
            }
          }
          if (bytesRead < 0) success = false;
          outFile.close();
          success = success && Storage.replaceFile(tmp.c_str(), CUSTOM_SLEEP_ROOT_BMP);
          if (!success) Storage.remove(tmp.c_str());
        }
      }
    }
  }

  if (success) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
  }
  return success;
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  const bool success = setSleepCoverFromFile(renderer, filePath);
  if (success) {
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::drawSquircleButtons() {
  if (!mappedInput.hasTouch() || !controlsVisible) return;

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  const int btnSize = 56;
  const int btnRadius = 16;
  const int marginX = 24;
  const int marginY = 24;

  const int leftX = marginX;
  const int leftY = pageHeight - marginY - btnSize;

  const int rightX = pageWidth - marginX - btnSize;
  const int rightY = leftY;

  // Sử dụng hàm chuẩn hóa OS của TouchHeaderBackButton
  TouchHeaderBackButton::drawSquircleBackButton(renderer, leftX, leftY, btnSize, btnRadius);
  TouchHeaderBackButton::drawSquircleMenuButton(renderer, rightX, rightY, btnSize, btnRadius);
}

void BmpViewerActivity::openViewerMenu() {
  std::string opt1 = tr(STR_SET_SLEEP_COVER);
  std::string opt2 = slideshowActive ? "Dừng trình chiếu" : "Trình chiếu ảnh (5 phút / ảnh)";
  std::string opt3 = "Xóa ảnh này";
  const char* const options[] = {opt1.c_str(), opt2.c_str(), opt3.c_str()};

  menuPopup.show("Tùy chọn", options, 3, 0, [this](int selected) {
    if (selected == 0) {
      doSetSleepCover();
    } else if (selected == 1) {
      slideshowActive = !slideshowActive;
      if (slideshowActive) {
        lastSlideshowAdvanceMillis = millis();
        controlsVisible = false;
        GUI.drawPopup(renderer, "Bắt đầu trình chiếu (5 phút)");
        delay(1200);
        onEnter();
      } else {
        GUI.drawPopup(renderer, "Đã dừng trình chiếu");
        delay(1200);
        onEnter();
      }
    } else if (selected == 2) {
      auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(
          renderer, mappedInput, "Xóa ảnh này?",
          filePath.substr(filePath.find_last_of('/') + 1));
      if (confirmation) {
        startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            Storage.remove(filePath.c_str());
            loadSiblingImages();
            if (siblingImages.empty()) {
              activityManager.goToFileBrowser(FsHelpers::extractFolderPath(filePath));
            } else {
              if (currentImageIndex >= static_cast<int>(siblingImages.size())) {
                currentImageIndex = 0;
              }
              std::string dirPath = FsHelpers::extractFolderPath(filePath);
              if (dirPath.back() != '/') dirPath += "/";
              filePath = dirPath + siblingImages[currentImageIndex];
              onEnter();
            }
          } else {
            onEnter();
          }
        });
      }
    }
  });

  menuPopup.processRender(renderer, mappedInput);
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0 || siblingImages.empty()) {
      return false;
    }
    int nextIndex = currentImageIndex + delta;
    if (slideshowActive) {
      if (nextIndex >= static_cast<int>(siblingImages.size())) {
        nextIndex = 0;
      } else if (nextIndex < 0) {
        nextIndex = static_cast<int>(siblingImages.size()) - 1;
      }
    } else {
      if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
        return false;
      }
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + siblingImages[currentImageIndex];
    onEnter();
    return true;
  };

  if (menuPopup.isActive()) {
    if (menuPopup.handleInput(mappedInput, [this] {
          if (menuPopup.isActive()) {
            menuPopup.processRender(renderer, mappedInput);
          } else {
            onEnter();
          }
        })) {
      return;
    }
  }

  // Slideshow advance (every 5 minutes = 300,000 ms)
  if (slideshowActive && !menuPopup.isActive()) {
    if (millis() - lastSlideshowAdvanceMillis >= 5 * 60 * 1000UL) {
      lastSlideshowAdvanceMillis = millis();
      openSibling(1);
      return;
    }
  }

  // Auto-hide controls after 5 seconds of inactivity
  if (controlsVisible && !menuPopup.isActive() && !slideshowActive) {
    if (millis() - controlsShownMillis > 5000UL) {
      controlsVisible = false;
      onEnter();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (returnToCaller) {
      finish();
      return;
    }
    activityManager.goToFileBrowser(filePath);
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    openSibling(1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    openSibling(-1);
    return;
  }

  // Touch handling
  if (mappedInput.hasTouch() && !menuPopup.isActive()) {
    int tapX = 0, tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
      const int pageWidth = renderer.getScreenWidth();
      const int pageHeight = renderer.getScreenHeight();

      if (controlsVisible) {
        // Back Button touch zone (góc dưới bên trái)
        if (tapX >= 0 && tapX <= 110 && tapY >= pageHeight - 120 && tapY <= pageHeight) {
          if (returnToCaller) {
            finish();
            return;
          }
          activityManager.goToFileBrowser(filePath);
          return;
        }

        // Menu Button touch zone (góc dưới bên phải)
        if (tapX >= pageWidth - 110 && tapX <= pageWidth && tapY >= pageHeight - 120 && tapY <= pageHeight) {
          openViewerMenu();
          return;
        }
      }

      // GESTURE CHẠM 2 CẠNH BÊN ĐỂ CHUYỂN ẢNH TRÁI / PHẢI:
      // Chạm cạnh bên trái (< 28% chiều rộng): Lùi về ảnh trước
      if (tapX < pageWidth * 0.28) {
        openSibling(-1);
        return;
      }
      // Chạm cạnh bên phải (> 72% chiều rộng): Tiến sang ảnh tiếp theo
      if (tapX > pageWidth * 0.72) {
        openSibling(1);
        return;
      }

      // Chạm vùng giữa (28% - 72%): Bật / tắt thanh điều khiển (controls)
      controlsVisible = !controlsVisible;
      if (controlsVisible) {
        controlsShownMillis = millis();
      }
      onEnter();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openViewerMenu();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}

