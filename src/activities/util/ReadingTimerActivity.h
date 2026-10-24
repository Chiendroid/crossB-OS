#pragma once

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"

class ReadingTimerActivity final : public Activity {
 private:
  uint8_t selectedMinutes = 0;

  void renderHeader();
  void renderMainCard();
  void renderPresets();
  void renderFooter();
  void handleTouch(int x, int y);
  void saveAndExit();

 public:
  ReadingTimerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ReadingTimerActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
