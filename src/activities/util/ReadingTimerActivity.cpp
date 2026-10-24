#include "ReadingTimerActivity.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "CrossPointSettings.h"
#include "HalDisplay.h"
#include "activities/ActivityManager.h"
#include "activities/ActivityResult.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "fontIds.h"

extern ActivityManager activityManager;

ReadingTimerActivity::ReadingTimerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ReadingTimer", renderer, mappedInput) {}

void ReadingTimerActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();
  selectedMinutes = SETTINGS.readingTimerMinutes;
  requestUpdate(true);
}

void ReadingTimerActivity::renderHeader() {
  const int screenWidth = renderer.getScreenWidth();
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 20, 14, 40, 10);

  const char* title = "Hẹn Giờ Đọc Sách";
  const int tw = renderer.getTextWidth(UI_12_FONT_ID, title, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, (screenWidth - tw) / 2, 24, title, true, EpdFontFamily::BOLD);
}

void ReadingTimerActivity::renderMainCard() {
  const int screenW = renderer.getScreenWidth();
  const int cardX = 20;
  const int cardY = 72;
  const int cardW = screenW - (cardX * 2);
  const int cardH = 118;

  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 16, true);

  // Hiển thị giá trị số phút lớn bên trái / giữa
  char valBuf[16];
  snprintf(valBuf, sizeof(valBuf), "%d", selectedMinutes);
  const int valW = renderer.getTextWidth(NOTOSANS_16_FONT_ID, valBuf, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_16_FONT_ID, cardX + 36, cardY + 38, valBuf, true, EpdFontFamily::BOLD);

  const char* unitText = (selectedMinutes == 0) ? "phút (Đang tắt)" : "phút đọc sách";
  renderer.drawText(UI_12_FONT_ID, cardX + 44 + valW, cardY + 44, unitText, true, EpdFontFamily::BOLD);

  // Cặp nút bấm dọc [ ▲ ] và [ ▼ ] ở bên phải (+- 1 đơn vị)
  const int btnW = 54;
  const int btnH = 46;
  const int btnX = cardX + cardW - btnW - 14;

  // Nút tăng (+1)
  const int upY = cardY + 10;
  renderer.fillRoundedRect(btnX, upY, btnW, btnH, 10, Color::White);
  renderer.drawRoundedRect(btnX, upY, btnW, btnH, 2, 10, true);
  const int cx = btnX + btnW / 2;
  const int upCy = upY + btnH / 2;
  renderer.drawLine(cx, upCy - 5, cx - 8, upCy + 4, 2, true);
  renderer.drawLine(cx, upCy - 5, cx + 8, upCy + 4, 2, true);

  // Nút giảm (-1)
  const int downY = cardY + 62;
  renderer.fillRoundedRect(btnX, downY, btnW, btnH, 10, Color::White);
  renderer.drawRoundedRect(btnX, downY, btnW, btnH, 2, 10, true);
  const int downCy = downY + btnH / 2;
  renderer.drawLine(cx, downCy + 5, cx - 8, downCy - 4, 2, true);
  renderer.drawLine(cx, downCy + 5, cx + 8, downCy - 4, 2, true);
}

void ReadingTimerActivity::renderPresets() {
  const int cardX = 20;
  const int labelY = 212;
  renderer.drawText(UI_10_FONT_ID, cardX + 4, labelY, "Mốc cài đặt nhanh:", true, EpdFontFamily::BOLD);

  // 5 mốc preset: [Tắt] [15p] [25p] [45p] [60p]
  static const struct {
    uint8_t mins;
    const char* label;
  } presets[] = {
      {0, "Tắt (0p)"},
      {15, "15 phút"},
      {25, "25 phút"},
      {45, "45 phút"},
      {60, "60 phút"},
  };

  const int pillY = 238;
  const int pillH = 38;
  const int totalW = renderer.getScreenWidth() - 40;
  const int spacing = 8;
  const int pillW = (totalW - spacing * 4) / 5;

  for (int i = 0; i < 5; ++i) {
    const int px = cardX + i * (pillW + spacing);
    const bool isSelected = (selectedMinutes == presets[i].mins);

    if (isSelected) {
      renderer.fillRoundedRect(px, pillY, pillW, pillH, 10, Color::Black);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, presets[i].label, EpdFontFamily::BOLD);
      renderer.drawText(UI_10_FONT_ID, px + (pillW - tw) / 2, pillY + 11, presets[i].label, false, EpdFontFamily::BOLD);
    } else {
      renderer.fillRoundedRect(px, pillY, pillW, pillH, 10, Color::White);
      renderer.drawRoundedRect(px, pillY, pillW, pillH, 1, 10, true);
      const int tw = renderer.getTextWidth(UI_10_FONT_ID, presets[i].label, EpdFontFamily::REGULAR);
      renderer.drawText(UI_10_FONT_ID, px + (pillW - tw) / 2, pillY + 11, presets[i].label, true, EpdFontFamily::REGULAR);
    }
  }
}

void ReadingTimerActivity::renderFooter() {
  const int screenW = renderer.getScreenWidth();
  const int cardX = 20;
  const int cardW = screenW - (cardX * 2);

  // 1. Nút mở bàn phím
  const int kbY = 320;
  const int kbH = 48;
  renderer.drawRoundedRect(cardX, kbY, cardW, kbH, 1, 12, true);
  const char* kbText = "[ Gõ số ] Mở bàn phím nhập số phút tự do...";
  const int ktw = renderer.getTextWidth(UI_10_FONT_ID, kbText, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, (screenW - ktw) / 2, kbY + 15, kbText, true, EpdFontFamily::REGULAR);

  // 2. Nút bấm chính: Lưu & Kích hoạt
  const int saveY = 388;
  const int saveH = 54;
  renderer.fillRoundedRect(cardX, saveY, cardW, saveH, 14, Color::Black);
  const char* saveText = "Lưu & Kích hoạt";
  const int stw = renderer.getTextWidth(UI_12_FONT_ID, saveText, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, (screenW - stw) / 2, saveY + 17, saveText, false, EpdFontFamily::BOLD);
}

void ReadingTimerActivity::saveAndExit() {
  SETTINGS.readingTimerMinutes = selectedMinutes;
  SETTINGS.saveToFile();
  activityManager.goToPlugins(false);
}

void ReadingTimerActivity::handleTouch(const int x, const int y) {
  const int screenW = renderer.getScreenWidth();
  const int cardX = 20;
  const int cardW = screenW - 40;

  // 1. Nút Back (top-left squircle)
  if (x >= 14 && x <= 66 && y >= 8 && y <= 60) {
    activityManager.goToPlugins(false);
    return;
  }

  // 2. Cặp nút ▲ / ▼ ở thẻ chính
  const int btnW = 54;
  const int btnH = 46;
  const int btnX = cardX + cardW - btnW - 14;
  const int upY = 72 + 10;
  const int downY = 72 + 62;

  if (x >= btnX && x <= btnX + btnW) {
    if (y >= upY && y <= upY + btnH) {
      if (selectedMinutes < 240) selectedMinutes++;
      requestUpdate(true);
      return;
    }
    if (y >= downY && y <= downY + btnH) {
      if (selectedMinutes > 0) selectedMinutes--;
      requestUpdate(true);
      return;
    }
  }

  // 3. Chạm vào 5 mốc presets
  const int pillY = 238;
  const int pillH = 38;
  if (y >= pillY && y <= pillY + pillH) {
    const int totalW = screenW - 40;
    const int spacing = 8;
    const int pillW = (totalW - spacing * 4) / 5;
    static const uint8_t presetVals[] = {0, 15, 25, 45, 60};
    for (int i = 0; i < 5; ++i) {
      const int px = cardX + i * (pillW + spacing);
      if (x >= px && x <= px + pillW) {
        selectedMinutes = presetVals[i];
        requestUpdate(true);
        return;
      }
    }
  }

  // 4. Nút mở bàn phím
  if (x >= cardX && x <= cardX + cardW && y >= 320 && y <= 368) {
    char initVal[16];
    snprintf(initVal, sizeof(initVal), "%u", selectedMinutes);
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Nhập số phút đếm ngược (0-240):", initVal),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            const auto* kbResult = std::get_if<KeyboardResult>(&result.data);
            if (kbResult && !kbResult->text.empty()) {
              const int val = std::atoi(kbResult->text.c_str());
              selectedMinutes = static_cast<uint8_t>(std::clamp(val, 0, 240));
              requestUpdate(true);
            }
          }
        });
    return;
  }

  // 5. Nút Lưu & Kích hoạt
  if (x >= cardX && x <= cardX + cardW && y >= 388 && y <= 442) {
    saveAndExit();
    return;
  }
}

void ReadingTimerActivity::loop() {
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    handleTouch(tx, ty);
    return;
  }

  if (mappedInput.wasBackGesture() || mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToPlugins(false);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    saveAndExit();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    if (selectedMinutes < 240) selectedMinutes++;
    requestUpdate(true);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    if (selectedMinutes > 0) selectedMinutes--;
    requestUpdate(true);
    return;
  }
}

void ReadingTimerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderHeader();
  renderMainCard();
  renderPresets();
  renderFooter();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
