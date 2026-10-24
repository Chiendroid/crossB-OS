#pragma once

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class GalleryActivity final : public UiListActivity {
 public:
  explicit GalleryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~GalleryActivity() override = default;

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return static_cast<int>(imageFiles.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;
  void drawChrome() override;
  void drawFooter() override;

 private:
  struct ImageEntry {
    std::string fullPath;
    std::string filename;
    std::string displayName;
    std::string sizeText;
    std::string extText;
    bool isCurrentWallpaper = false;
  };

  std::vector<ImageEntry> imageFiles;
  OptionPopup optionPopup;

  void scanImages();
  void showImageActions(int index);
  void setAsSleepCover(int index);
  void deleteImage(int index);
};
