#include "CalendarAppActivity.h"

#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "activities/ActivityManager.h"
#include "activities/boot_sleep/ClockDigits.h"
#include "components/icons/touchHeaderIcons.h"
#include "fontIds.h"

namespace {

static void drawFreeInkIcon(const GfxRenderer& renderer, const freeink::Icon& icon, int x, int y) {
  const int bytesPerRow = (icon.w + 7) / 8;
  for (int r = 0; r < icon.h; ++r) {
    for (int c = 0; c < icon.w; ++c) {
      uint8_t byteVal = icon.bits[r * bytesPerRow + (c / 8)];
      bool isBlack = ((byteVal & (0x80 >> (c % 8))) == 0);
      if (isBlack) {
        renderer.drawPixel(x + c, y + r, true);
      }
    }
  }
}

// Bảng 6 giờ hoàng đạo dân gian theo Chi của ngày
struct HoangDaoHourInfo {
  const char* col1[3];
  const char* col2[3];
};

HoangDaoHourInfo getHoangDaoDetails(int day, int month, int year) {
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  int jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  int chi = (jd + 1) % 12;
  if (chi < 0) chi += 12;

  // 6 nhóm theo ngày Tý/Ngọ, Sửu/Mùi, Dần/Thân, Mão/Dậu, Thìn/Tuất, Tỵ/Hợi
  int group = 5;
  switch (chi) {
    case 0: case 6: group = 0; break;
    case 1: case 7: group = 1; break;
    case 2: case 8: group = 2; break;
    case 3: case 9: group = 3; break;
    case 4: case 10: group = 4; break;
    default: group = 5; break;
  }

  static const HoangDaoHourInfo kInfos[6] = {
      {{"Tý (23h-01h)", "Sửu (01h-03h)", "Mão (05h-07h)"},
       {"Ngọ (11h-13h)", "Thân (15h-17h)", "Dậu (17h-19h)"}},
      {{"Dần (03h-05h)", "Mão (05h-07h)", "Tỵ (09h-11h)"},
       {"Thân (15h-17h)", "Tuất (19h-21h)", "Hợi (21h-23h)"}},
      {{"Tý (23h-01h)", "Sửu (01h-03h)", "Thìn (07h-09h)"},
       {"Tỵ (09h-11h)", "Mùi (13h-15h)", "Tuất (19h-21h)"}},
      {{"Tý (23h-01h)", "Dần (03h-05h)", "Mão (05h-07h)"},
       {"Ngọ (11h-13h)", "Mùi (13h-15h)", "Dậu (17h-19h)"}},
      {{"Dần (03h-05h)", "Thìn (07h-09h)", "Tỵ (09h-11h)"},
       {"Thân (15h-17h)", "Dậu (17h-19h)", "Hợi (21h-23h)"}},
      {{"Sửu (01h-03h)", "Thìn (07h-09h)", "Ngọ (11h-13h)"},
       {"Mùi (13h-15h)", "Tuất (19h-21h)", "Hợi (21h-23h)"}}
  };

  return kInfos[group];
}

// Bảng hướng Hỷ thần / Tài thần theo Can của ngày
const char* getDirectionInfo(const std::string& dayCanChi) {
  if (dayCanChi.rfind("Giáp", 0) == 0) return "Hỷ Thần: Đông Bắc  ·  Tài Thần: Đông Nam";
  if (dayCanChi.rfind("Ất", 0) == 0)   return "Hỷ Thần: Tây Bắc   ·  Tài Thần: Đông Nam";
  if (dayCanChi.rfind("Bính", 0) == 0) return "Hỷ Thần: Tây Nam   ·  Tài Thần: Chính Đông";
  if (dayCanChi.rfind("Đinh", 0) == 0) return "Hỷ Thần: Chính Nam ·  Tài Thần: Chính Đông";
  if (dayCanChi.rfind("Mậu", 0) == 0)  return "Hỷ Thần: Đông Nam  ·  Tài Thần: Chính Bắc";
  if (dayCanChi.rfind("Kỷ", 0) == 0)   return "Hỷ Thần: Đông Bắc  ·  Tài Thần: Chính Nam";
  if (dayCanChi.rfind("Canh", 0) == 0) return "Hỷ Thần: Tây Bắc   ·  Tài Thần: Chính Tây";
  if (dayCanChi.rfind("Tân", 0) == 0)  return "Hỷ Thần: Tây Nam   ·  Tài Thần: Chính Tây";
  if (dayCanChi.rfind("Nhâm", 0) == 0) return "Hỷ Thần: Chính Nam ·  Tài Thần: Chính Nam";
  return "Hỷ Thần: Đông Nam  ·  Tài Thần: Chính Bắc";
}

}  // namespace

CalendarAppActivity::CalendarAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("CalendarApp", renderer, mappedInput) {}

void CalendarAppActivity::onEnter() {
  Activity::onEnter();
  renderer.clearScreen();

  uint16_t y = 2026;
  uint8_t m = 10, d = 23, h = 0, min = 0;
  if (halClock.isAvailable()) {
    halClock.getDateTime(y, m, d, h, min);
    currentYear = y;
    currentMonth = m;
    currentDay = d;
  }
  displayedYear = currentYear;
  displayedMonth = currentMonth;
  selectedDay = currentDay;

  requestUpdate();
}

void CalendarAppActivity::onExit() {
  Activity::onExit();
}

void CalendarAppActivity::setViewMode(CalendarViewMode mode) {
  if (viewMode != mode) {
    viewMode = mode;
    requestUpdate();
  }
}

void CalendarAppActivity::nextMonth() {
  displayedMonth++;
  if (displayedMonth > 12) {
    displayedMonth = 1;
    displayedYear++;
  }
  int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  if (selectedDay > maxDays) selectedDay = maxDays;
  requestUpdate();
}

void CalendarAppActivity::prevMonth() {
  displayedMonth--;
  if (displayedMonth < 1) {
    displayedMonth = 12;
    displayedYear--;
  }
  int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  if (selectedDay > maxDays) selectedDay = maxDays;
  requestUpdate();
}

void CalendarAppActivity::nextDay() {
  int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  selectedDay++;
  if (selectedDay > maxDays) {
    selectedDay = 1;
    nextMonth();
  } else {
    requestUpdate();
  }
}

void CalendarAppActivity::prevDay() {
  selectedDay--;
  if (selectedDay < 1) {
    prevMonth();
    selectedDay = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
    requestUpdate();
  } else {
    requestUpdate();
  }
}

void CalendarAppActivity::goToToday() {
  displayedYear = currentYear;
  displayedMonth = currentMonth;
  selectedDay = currentDay;
  requestUpdate();
}

void CalendarAppActivity::selectDay(int day) {
  int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  if (day >= 1 && day <= maxDays) {
    selectedDay = day;
    requestUpdate();
  }
}

void CalendarAppActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToPlugins();
    return;
  }

  // Vuốt ngang
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left) {
    if (viewMode == CalendarViewMode::DAY) {
      nextDay();
    } else {
      nextMonth();
    }
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right) {
    if (viewMode == CalendarViewMode::DAY) {
      prevDay();
    } else {
      prevMonth();
    }
    return;
  }

  // Phím bấm vật lý
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (viewMode == CalendarViewMode::DAY) {
      prevDay();
    } else {
      prevMonth();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (viewMode == CalendarViewMode::DAY) {
      nextDay();
    } else {
      nextMonth();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    goToToday();
    return;
  }

  int touchX = 0, touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int screenWidth = renderer.getScreenWidth();

    // 1. Nút Back góc trái trên
    if (touchX < 55 && touchY < 52) {
      activityManager.goToPlugins();
      return;
    }

    // 2. Chuyển Tab Xem: [ Lịch Tháng ] (X: 130..250) và [ Lịch Ngày ] (X: 250..370)
    if (touchY >= 8 && touchY <= 44) {
      if (touchX >= 130 && touchX <= 250) {
        setViewMode(CalendarViewMode::MONTH);
        return;
      }
      if (touchX >= 250 && touchX <= 370) {
        setViewMode(CalendarViewMode::DAY);
        return;
      }
    }

    // 3. Nút [ Hôm nay ] góc phải trên
    if (touchX > screenWidth - 95 && touchY < 52) {
      goToToday();
      return;
    }

    // --- XỬ LÝ CHẠM TRONG CHẾ ĐỘ THÁNG ---
    if (viewMode == CalendarViewMode::MONTH) {
      // Nút điều hướng tháng < và >
      if (touchY >= 58 && touchY <= 96) {
        if (touchX >= 10 && touchX <= 60) {
          prevMonth();
          return;
        }
        if (touchX >= 230 && touchX <= 280) {
          nextMonth();
          return;
        }
      }

      // Chạm vào các ô trong lưới tháng
      const int gridX = 12;
      const int gridY = 126;
      const int gridW = screenWidth - (gridX * 2);
      const int colW = gridW / 7;
      const int rowH = 54;

      if (touchX >= gridX && touchX <= gridX + colW * 7 && touchY >= gridY && touchY <= gridY + rowH * 6) {
        int col = (touchX - gridX) / colW;
        int row = (touchY - gridY) / rowH;
        if (col >= 0 && col < 7 && row >= 0 && row < 6) {
          int firstDow = LunarCalendar::dayOfWeek(1, displayedMonth, displayedYear);
          int dayVal = row * 7 + col - firstDow + 1;
          int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
          if (dayVal >= 1 && dayVal <= maxDays) {
            if (selectedDay == dayVal) {
              // Bấm lần nữa vào ngày đang chọn -> Mở Tờ Lịch Ngày!
              setViewMode(CalendarViewMode::DAY);
            } else {
              selectDay(dayVal);
            }
            return;
          }
        }
      }

      // Chạm vào thẻ tóm tắt ngày ở nửa dưới -> Chuyển sang xem Tờ Lịch Ngày
      if (touchY >= 460) {
        setViewMode(CalendarViewMode::DAY);
        return;
      }
    }

    // --- XỬ LÝ CHẠM TRONG CHẾ ĐỘ NGÀY ---
    if (viewMode == CalendarViewMode::DAY) {
      // Nút chuyển ngày dưới đáy màn hình (Y: 670..750)
      if (touchY >= 670 && touchY <= 760) {
        if (touchX >= 25 && touchX <= 170) {
          prevDay();
          return;
        }
        if (touchX >= 180 && touchX <= 290) {
          goToToday();
          return;
        }
        if (touchX >= 300 && touchX <= 455) {
          nextDay();
          return;
        }
      }
    }
  }
}

void CalendarAppActivity::renderHeader() {
  const int screenWidth = renderer.getScreenWidth();
  const int headerH = 52;

  // Đường kẻ mỏng 1px dưới header
  renderer.drawLine(0, headerH, screenWidth, headerH, true);

  // Icon Back 32x32
  drawFreeInkIcon(renderer, icon_back_32, 12, 10);

  // Segmented Control [ Tháng ] | [ Ngày ]
  const int segY = 10;
  const int segH = 32;
  const int segTabW = 105;
  const int segX1 = (screenWidth / 2) - segTabW;
  const int segX2 = screenWidth / 2;

  // Khung viền ngoài
  renderer.drawRoundedRect(segX1, segY, segTabW * 2, segH, 1, 8, true);

  // Tab Tháng
  if (viewMode == CalendarViewMode::MONTH) {
    renderer.fillRoundedRect(segX1, segY, segTabW, segH, 8, Color::Black);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Lịch Tháng", EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, segX1 + (segTabW - tw) / 2, segY + 6, "Lịch Tháng", false, EpdFontFamily::BOLD);
  } else {
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Lịch Tháng", EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, segX1 + (segTabW - tw) / 2, segY + 6, "Lịch Tháng", true, EpdFontFamily::REGULAR);
  }

  // Tab Ngày
  if (viewMode == CalendarViewMode::DAY) {
    renderer.fillRoundedRect(segX2, segY, segTabW, segH, 8, Color::Black);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Tờ Lịch Ngày", EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, segX2 + (segTabW - tw) / 2, segY + 6, "Tờ Lịch Ngày", false, EpdFontFamily::BOLD);
  } else {
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Tờ Lịch Ngày", EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, segX2 + (segTabW - tw) / 2, segY + 6, "Tờ Lịch Ngày", true, EpdFontFamily::REGULAR);
  }

  // Nút [ Hôm nay ] góc phải trên
  const int todayBtnW = 76;
  const int todayBtnH = 32;
  const int todayBtnX = screenWidth - todayBtnW - 10;
  renderer.drawRoundedRect(todayBtnX, segY, todayBtnW, todayBtnH, 1, 8, true);
  int ttw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm nay", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, todayBtnX + (todayBtnW - ttw) / 2, segY + 6, "Hôm nay", true, EpdFontFamily::REGULAR);
}

void CalendarAppActivity::renderMonthView() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // 1. Thanh tiêu đề Tháng / Năm với nút chuyển tháng to rõ
  const int titleBarY = 60;
  // Nút < tháng trước
  renderer.drawRoundedRect(14, titleBarY, 40, 32, 1, 8, true);
  renderer.drawText(UI_12_FONT_ID, 28, titleBarY + 5, "<", true, EpdFontFamily::BOLD);

  // Tiêu đề tháng năm FONT TO ĐẬM (LEXENDDECA_16_FONT_ID)
  char monthTitle[48];
  snprintf(monthTitle, sizeof(monthTitle), "Tháng %d · %d", displayedMonth, displayedYear);
  renderer.drawText(LEXENDDECA_16_FONT_ID, 64, titleBarY + 2, monthTitle, true, EpdFontFamily::BOLD);

  // Nút > tháng sau
  int titleTw = renderer.getTextWidth(LEXENDDECA_16_FONT_ID, monthTitle, EpdFontFamily::BOLD);
  int nextBtnX = 64 + titleTw + 12;
  renderer.drawRoundedRect(nextBtnX, titleBarY, 40, 32, 1, 8, true);
  renderer.drawText(UI_12_FONT_ID, nextBtnX + 14, titleBarY + 5, ">", true, EpdFontFamily::BOLD);

  // 2. Thanh tiêu đề Thứ (Thứ Hai -> Chủ Nhật)
  const int gridX = 12;
  const int gridW = screenWidth - (gridX * 2);
  const int colW = gridW / 7;
  const int dowY = 100;
  static const char* const kDows[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};

  for (int i = 0; i < 7; ++i) {
    int colCenter = gridX + i * colW + (colW / 2);
    int tw = renderer.getTextWidth(LEXENDDECA_10_FONT_ID, kDows[i], EpdFontFamily::BOLD);
    renderer.drawText(LEXENDDECA_10_FONT_ID, colCenter - (tw / 2), dowY, kDows[i], true, EpdFontFamily::BOLD);
  }
  renderer.drawLine(gridX, dowY + 20, gridX + colW * 7, dowY + 20, true);

  // 3. Lưới Tháng - FONT TO, RÕ NÉT, DỄ NHÌN
  const int gridY = 124;
  const int rowH = 54;
  const int daysInMonth = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  const int firstDow = LunarCalendar::dayOfWeek(1, displayedMonth, displayedYear);

  int totalRows = (firstDow + daysInMonth + 6) / 7;
  if (totalRows < 5) totalRows = 5;

  for (int dayVal = 1; dayVal <= daysInMonth; ++dayVal) {
    int slot = firstDow + dayVal - 1;
    int col = slot % 7;
    int row = slot / 7;

    int cellX = gridX + col * colW;
    int cellY = gridY + row * rowH;

    bool isToday = (dayVal == currentDay && displayedMonth == currentMonth && displayedYear == currentYear);
    bool isSelected = (dayVal == selectedDay);

    LunarDate lDate = LunarCalendar::solarToLunar(dayVal, displayedMonth, displayedYear);

    // Vẽ khung viền ô
    if (isSelected) {
      // Ô được chọn: Nền đen chữ trắng cực kỳ nổi bật!
      renderer.fillRoundedRect(cellX + 2, cellY + 2, colW - 4, rowH - 4, 8, Color::Black);
    } else if (isToday) {
      // Ô hôm nay: Viền đôi bo góc rõ nét
      renderer.drawRoundedRect(cellX + 2, cellY + 2, colW - 4, rowH - 4, 2, 8, true);
    } else {
      // Viền mỏng phân cách ô nhẹ nhàng
      renderer.drawRoundedRect(cellX + 2, cellY + 2, colW - 4, rowH - 4, 1, 8, true);
    }

    // Ngày Dương lịch (FONT 14PT TO RÕ)
    char solarStr[8];
    snprintf(solarStr, sizeof(solarStr), "%d", dayVal);
    int sTw = renderer.getTextWidth(LEXENDDECA_14_FONT_ID, solarStr, EpdFontFamily::BOLD);
    renderer.drawText(LEXENDDECA_14_FONT_ID, cellX + (colW - sTw) / 2, cellY + 4, solarStr, !isSelected,
                      EpdFontFamily::BOLD);

    // Ngày Âm lịch (FONT UI_10 TO RÕ)
    char lunarStr[16];
    if (lDate.day == 1) {
      snprintf(lunarStr, sizeof(lunarStr), "1/%d", lDate.month);
    } else {
      snprintf(lunarStr, sizeof(lunarStr), "%d", lDate.day);
    }

    bool isSpecialLunar = (lDate.day == 1 || lDate.day == 15 || lDate.lunarHoliday != nullptr);
    int lTw = renderer.getTextWidth(UI_10_FONT_ID, lunarStr, isSpecialLunar ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, cellX + (colW - lTw) / 2, cellY + 28, lunarStr, !isSelected,
                      isSpecialLunar ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // Chấm nhỏ báo ngày lễ
    if (lDate.lunarHoliday != nullptr || lDate.solarHoliday != nullptr) {
      if (isSelected) {
        renderer.fillRoundedRect(cellX + (colW / 2) - 2, cellY + rowH - 8, 4, 4, 2, Color::White);
      } else {
        renderer.fillRoundedRect(cellX + (colW / 2) - 2, cellY + rowH - 8, 4, 4, 2, Color::Black);
      }
    }
  }

  // 4. Thẻ Chi Tiết Ngày Được Chọn Ở Dưới Lưới Tháng (Card Thông Tin)
  const int cardY = gridY + totalRows * rowH + 6;
  const int cardW = screenWidth - (gridX * 2);
  const int cardH = screenHeight - cardY - 14;

  renderer.drawRoundedRect(gridX, cardY, cardW, cardH, 1, 14, true);

  LunarDate selLunar = LunarCalendar::solarToLunar(selectedDay, displayedMonth, displayedYear);
  const char* dowName = LunarCalendar::dayOfWeekName(selectedDay, displayedMonth, displayedYear);

  // Tiêu đề Dương Lịch: Thứ, ngày tháng năm (FONT 14PT BOLD)
  char selSolarBuf[96];
  snprintf(selSolarBuf, sizeof(selSolarBuf), "%s, %d tháng %d, %d", dowName, selectedDay, displayedMonth, displayedYear);
  renderer.drawText(LEXENDDECA_14_FONT_ID, gridX + 16, cardY + 12, selSolarBuf, true, EpdFontFamily::BOLD);

  // Tiêu đề Âm Lịch: Ngày X tháng Y năm Can Chi (FONT 12PT BOLD)
  char selLunarBuf[96];
  if (selLunar.isLeap) {
    snprintf(selLunarBuf, sizeof(selLunarBuf), "Ngày %d/%d (Nhuận) · Năm %s", selLunar.day, selLunar.month,
             selLunar.yearCanChi.c_str());
  } else {
    snprintf(selLunarBuf, sizeof(selLunarBuf), "Ngày %d/%d Âm lịch · Năm %s", selLunar.day, selLunar.month,
             selLunar.yearCanChi.c_str());
  }
  renderer.drawText(LEXENDDECA_12_FONT_ID, gridX + 16, cardY + 38, selLunarBuf, true, EpdFontFamily::BOLD);

  renderer.drawLine(gridX + 14, cardY + 62, gridX + cardW - 14, cardY + 62, true);

  // Tam Trụ Can Chi
  char ccFull[128];
  snprintf(ccFull, sizeof(ccFull), "Can Chi: Ngày %s | Tháng %s | Năm %s", selLunar.dayCanChi.c_str(),
           selLunar.monthCanChi.c_str(), selLunar.yearCanChi.c_str());
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 70, ccFull, true, EpdFontFamily::REGULAR);

  // Sự kiện / Ngày lễ (nếu có)
  int rowInfoY = cardY + 94;
  if (selLunar.lunarHoliday != nullptr || selLunar.solarHoliday != nullptr || selLunar.day == 15 || selLunar.day == 1) {
    std::string hStr;
    if (selLunar.lunarHoliday != nullptr) hStr = selLunar.lunarHoliday;
    else if (selLunar.solarHoliday != nullptr) hStr = selLunar.solarHoliday;
    else if (selLunar.day == 15) hStr = "Ngày Rằm (Vọng)";
    else if (selLunar.day == 1) hStr = "Mùng Một (Sóc)";
    renderer.drawText(UI_10_FONT_ID, gridX + 16, rowInfoY, hStr.c_str(), true, EpdFontFamily::BOLD);
    rowInfoY += 24;
  }

  // Giờ Hoàng Đạo
  HoangDaoHourInfo hd = getHoangDaoDetails(selectedDay, displayedMonth, displayedYear);
  char hdSummary[128];
  snprintf(hdSummary, sizeof(hdSummary), "Giờ tốt: %s, %s, %s...", hd.col1[0], hd.col1[1], hd.col2[0]);
  renderer.drawText(UI_10_FONT_ID, gridX + 16, rowInfoY, hdSummary, true, EpdFontFamily::REGULAR);

  // Nút gợi ý: [ Chạm để xem Tờ Lịch Bóc Ngày Này -> ]
  const int openDayBtnY = cardY + cardH - 36;
  renderer.drawRoundedRect(gridX + 14, openDayBtnY, cardW - 28, 28, 1, 6, true);
  const char* openDayHint = "Xem Tờ Lịch Bóc Ngày Chi Tiết  >";
  int odTw = renderer.getTextWidth(UI_10_FONT_ID, openDayHint, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, gridX + (cardW - odTw) / 2, openDayBtnY + 5, openDayHint, true, EpdFontFamily::BOLD);
}

void CalendarAppActivity::renderDayView() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  LunarDate selLunar = LunarCalendar::solarToLunar(selectedDay, displayedMonth, displayedYear);
  const char* dowName = LunarCalendar::dayOfWeekName(selectedDay, displayedMonth, displayedYear);

  // === TỜ LỊCH BÓC TRUYỀN THỐNG VIỆT NAM (BLOC LỊCH) TRÊN MÀN HÌNH DỌC ===
  const int cardX = 16;
  const int cardY = 60;
  const int cardW = screenWidth - (cardX * 2);
  const int cardH = screenHeight - cardY - 14;
  const int cardCenterX = cardX + (cardW / 2);

  // Khung ngoài bo góc lớn 16px, viền 2px dày dặn
  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 2, 16, true);

  // 1. GÁY NẸP LỊCH PHÍA TRÊN (Mô phỏng bloc lịch treo tường)
  const int topHeaderH = 34;
  renderer.fillRoundedRect(cardX + 2, cardY + 2, cardW - 4, topHeaderH, 14, Color::Black);
  renderer.fillRect(cardX + 2, cardY + topHeaderH - 6, cardW - 4, 8, Color::Black);

  const char* blocTitle = "LỊCH VẠN NIÊN VIỆT NAM";
  int btTw = renderer.getTextWidth(LEXENDDECA_10_FONT_ID, blocTitle, EpdFontFamily::BOLD);
  renderer.drawText(LEXENDDECA_10_FONT_ID, cardCenterX - (btTw / 2), cardY + 8, blocTitle, false, EpdFontFamily::BOLD);

  // 2. THÁNG & NĂM DƯƠNG LỊCH (CĂN GIỮA)
  char solarMonthYear[48];
  snprintf(solarMonthYear, sizeof(solarMonthYear), "THÁNG %d · NĂM %d", displayedMonth, displayedYear);
  int smTw = renderer.getTextWidth(LEXENDDECA_14_FONT_ID, solarMonthYear, EpdFontFamily::BOLD);
  renderer.drawText(LEXENDDECA_14_FONT_ID, cardCenterX - (smTw / 2), cardY + 44, solarMonthYear, true,
                    EpdFontFamily::BOLD);

  // 3. THỨ TRONG TUẦN (CHỮ HOA LỚN, CĂN GIỮA)
  char upperDow[32];
  snprintf(upperDow, sizeof(upperDow), "%s", dowName);
  for (int i = 0; upperDow[i]; ++i) upperDow[i] = (char)toupper((unsigned char)upperDow[i]);
  int dowTw = renderer.getTextWidth(LEXENDDECA_16_FONT_ID, upperDow, EpdFontFamily::BOLD);
  renderer.drawText(LEXENDDECA_16_FONT_ID, cardCenterX - (dowTw / 2), cardY + 70, upperDow, true,
                    EpdFontFamily::BOLD);

  // 4. CON SỐ NGÀY DƯƠNG LỊCH KHỔNG LỒ (Ubuntu-Bold 74pt ~53px cao, CĂN GIỮA TUYỆT ĐỐI)
  char dayStr[8];
  snprintf(dayStr, sizeof(dayStr), "%d", selectedDay);
  clock_font::drawClock(renderer, cardCenterX, cardY + 168, dayStr, true);

  // 5. ĐƯỜNG KẺ PHÂN CÁCH TRANG NHÃ
  renderer.drawLine(cardX + 24, cardY + 192, cardX + cardW - 24, cardY + 192, true);

  // 6. DÒNG ÂM LỊCH TO RÕ (CĂN GIỮA)
  char lunarDayStr[64];
  if (selLunar.isLeap) {
    snprintf(lunarDayStr, sizeof(lunarDayStr), "Ngày %d Tháng %d (Nhuận)", selLunar.day, selLunar.month);
  } else {
    snprintf(lunarDayStr, sizeof(lunarDayStr), "Ngày %d Tháng %d Âm Lịch", selLunar.day, selLunar.month);
  }
  int ldTw = renderer.getTextWidth(LEXENDDECA_16_FONT_ID, lunarDayStr, EpdFontFamily::BOLD);
  renderer.drawText(LEXENDDECA_16_FONT_ID, cardCenterX - (ldTw / 2), cardY + 204, lunarDayStr, true,
                    EpdFontFamily::BOLD);

  char lunarYearStr[48];
  snprintf(lunarYearStr, sizeof(lunarYearStr), "Năm %s", selLunar.yearCanChi.c_str());
  int lyTw = renderer.getTextWidth(LEXENDDECA_14_FONT_ID, lunarYearStr, EpdFontFamily::REGULAR);
  renderer.drawText(LEXENDDECA_14_FONT_ID, cardCenterX - (lyTw / 2), cardY + 232, lunarYearStr, true,
                    EpdFontFamily::REGULAR);

  // 7. SỰ KIỆN / NGÀY LỄ TRUYỀN THỐNG (BADGE SẠCH SẼ, KHÔNG DẤU SAO)
  int infoStartY = cardY + 266;
  if (selLunar.lunarHoliday != nullptr || selLunar.solarHoliday != nullptr || selLunar.day == 15 || selLunar.day == 1) {
    std::string badgeStr;
    if (selLunar.lunarHoliday != nullptr) badgeStr = selLunar.lunarHoliday;
    else if (selLunar.solarHoliday != nullptr) badgeStr = selLunar.solarHoliday;
    else if (selLunar.day == 15) badgeStr = "Ngày Rằm (Vọng)";
    else if (selLunar.day == 1) badgeStr = "Mùng Một (Sóc)";

    int bTw = renderer.getTextWidth(UI_10_FONT_ID, badgeStr.c_str(), EpdFontFamily::BOLD);
    int badgeW = bTw + 24;
    int badgeX = cardCenterX - (badgeW / 2);
    renderer.drawRoundedRect(badgeX, infoStartY, badgeW, 26, 1, 6, true);
    renderer.drawText(UI_10_FONT_ID, badgeX + 12, infoStartY + 4, badgeStr.c_str(), true, EpdFontFamily::BOLD);
    infoStartY += 36;
  }

  // 8. KHUNG CHI TIẾT PHONG THỦY & LỊCH VẠN NIÊN (CĂN ĐỀU CÁC CỘT)
  const int contentX = cardX + 24;
  const int contentW = cardW - 48;

  renderer.drawLine(contentX, infoStartY, contentX + contentW, infoStartY, true);
  infoStartY += 10;

  // Tam Trụ Can Chi (3 Cột Đều Nhau)
  const int colStep = contentW / 3;
  char ccFullDay[64], ccFullMonth[64], ccFullYear[64];
  snprintf(ccFullDay, sizeof(ccFullDay), "Ngày: %s", selLunar.dayCanChi.c_str());
  snprintf(ccFullMonth, sizeof(ccFullMonth), "Tháng: %s", selLunar.monthCanChi.c_str());
  snprintf(ccFullYear, sizeof(ccFullYear), "Năm: %s", selLunar.yearCanChi.c_str());

  renderer.drawText(UI_10_FONT_ID, contentX, infoStartY, ccFullDay, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, contentX + colStep, infoStartY, ccFullMonth, true, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, contentX + colStep * 2, infoStartY, ccFullYear, true, EpdFontFamily::BOLD);
  infoStartY += 28;

  // Giờ Hoàng Đạo (2 Cột Đều Nhau)
  renderer.drawLine(contentX, infoStartY, contentX + contentW, infoStartY, true);
  infoStartY += 8;

  renderer.drawText(LEXENDDECA_12_FONT_ID, contentX, infoStartY, "Giờ Hoàng Đạo (Tốt Trong Ngày):", true, EpdFontFamily::BOLD);
  infoStartY += 24;

  HoangDaoHourInfo hd = getHoangDaoDetails(selectedDay, displayedMonth, displayedYear);
  const int hdColW = contentW / 2;
  for (int i = 0; i < 3; ++i) {
    renderer.drawText(UI_10_FONT_ID, contentX + 8, infoStartY + i * 22, hd.col1[i], true, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, contentX + hdColW + 8, infoStartY + i * 22, hd.col2[i], true, EpdFontFamily::REGULAR);
  }
  infoStartY += 3 * 22 + 8;

  // Hướng Xuất Hành Dân Gian
  renderer.drawLine(contentX, infoStartY, contentX + contentW, infoStartY, true);
  infoStartY += 8;

  renderer.drawText(LEXENDDECA_12_FONT_ID, contentX, infoStartY, "Hướng Xuất Hành Dân Gian:", true, EpdFontFamily::BOLD);
  infoStartY += 22;

  const char* dirStr = getDirectionInfo(selLunar.dayCanChi);
  renderer.drawText(UI_10_FONT_ID, contentX + 8, infoStartY, dirStr, true, EpdFontFamily::REGULAR);
  infoStartY += 28;

  // Lời khuyên ngày
  renderer.drawLine(contentX, infoStartY, contentX + contentW, infoStartY, true);
  infoStartY += 8;

  renderer.drawText(LEXENDDECA_12_FONT_ID, contentX, infoStartY, "Lời Khuyên Ngày:", true, EpdFontFamily::BOLD);
  infoStartY += 22;

  const char* adviceStr = "Thuận lợi: Cúng tế, ký kết, cầu phúc, khởi công, xuất hành.";
  renderer.drawText(UI_10_FONT_ID, contentX + 8, infoStartY, adviceStr, true, EpdFontFamily::REGULAR);

  // 9. THANH ĐIỀU HƯỚNG NGÀY DƯỚI ĐÁY TỜ LỊCH (CĂN GIỮA TUYỆT ĐỐI)
  const int navY = cardY + cardH - 52;
  const int navBtnH = 38;
  const int prevW = 126;
  const int todayW = 100;
  const int nextW = 126;
  const int gap = 12;
  const int totalNavW = prevW + todayW + nextW + gap * 2;
  const int startNavX = (screenWidth - totalNavW) / 2;

  const int prevX = startNavX;
  renderer.drawRoundedRect(prevX, navY, prevW, navBtnH, 1, 8, true);
  int ptw = renderer.getTextWidth(UI_10_FONT_ID, "< Hôm trước", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, prevX + (prevW - ptw) / 2, navY + 9, "< Hôm trước", true, EpdFontFamily::REGULAR);

  const int todayX = prevX + prevW + gap;
  renderer.drawRoundedRect(todayX, navY, todayW, navBtnH, 1, 8, true);
  int ttw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm nay", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, todayX + (todayW - ttw) / 2, navY + 9, "Hôm nay", true, EpdFontFamily::REGULAR);

  const int nextX = todayX + todayW + gap;
  renderer.drawRoundedRect(nextX, navY, nextW, navBtnH, 1, 8, true);
  int ntw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm sau >", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, nextX + (nextW - ntw) / 2, navY + 9, "Hôm sau >", true, EpdFontFamily::REGULAR);
}

void CalendarAppActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  renderHeader();

  if (viewMode == CalendarViewMode::MONTH) {
    renderMonthView();
  } else {
    renderDayView();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
