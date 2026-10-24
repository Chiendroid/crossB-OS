#pragma once

#include <functional>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                    bool returnToCaller = false);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return slideshowActive; }
  static bool canSetSleepCover(const std::string& path);
  static bool setSleepCoverFromFile(GfxRenderer& renderer, const std::string& path);

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderImage();
  bool saveJpegSleepCover();
  static bool saveImageSleepCover(GfxRenderer& renderer, const std::string& path);
  void drawSquircleButtons();
  void openViewerMenu();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
  bool returnToCaller = false;

  // Touch controls
  bool controlsVisible = true;
  unsigned long controlsShownMillis = 0;

  // Slideshow (5 mins per image)
  bool slideshowActive = false;
  unsigned long lastSlideshowAdvanceMillis = 0;

  OptionPopup menuPopup;
};

