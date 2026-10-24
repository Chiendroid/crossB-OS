#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity {
  enum State {
    WIFI_SELECTION,
    CHECKING_FOR_UPDATE,
    VIEW_UPDATE,
    UPDATE_IN_PROGRESS,
    FAILED,
    FINISHED,
    SHUTTING_DOWN
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = WIFI_SELECTION;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;
  OptionPopup confirmPopup;

  std::vector<std::string> changelogLines;
  int scrollLine = 0;

  Rect updateButtonRect{0, 0, 0, 0};
  Rect changeChannelButtonRect{0, 0, 0, 0};
  Rect retryButtonRect{0, 0, 0, 0};
  Rect backButtonRect{0, 0, 0, 0};
  Rect changelogBoxRect{0, 0, 0, 0};

  void onWifiSelectionComplete(bool success);
  void startCheckForUpdate();
  void prepareChangelogLines();
  void runUpdateInstall();

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
