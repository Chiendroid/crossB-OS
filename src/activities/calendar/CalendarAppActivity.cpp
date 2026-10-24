#include "CalendarAppActivity.h"

#include <HalClock.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>

#include "activities/ActivityManager.h"
#include "activities/boot_sleep/ClockDigits.h"
#include "components/TouchHeaderBackButton.h"
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

// 12 Trực dân gian theo lịch vạn niên thiên văn truyền thống
struct TrucInfo {
  const char* name;
  const char* nature;
  const char* favorable;
  const char* unfavorable;
};

TrucInfo getTrucInfo(int day, int month, int year, const LunarDate& lunar) {
  int a = (14 - month) / 12;
  int y = year + 4800 - a;
  int m = month + 12 * a - 3;
  int jd = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  int dayChi = (jd + 1) % 12;
  if (dayChi < 0) dayChi += 12;

  // Chi của tháng âm: Tháng 1 là Dần (2), Tháng 2 là Mão (3)...
  int monthChi = (lunar.month + 1) % 12;
  int trucIndex = (dayChi - monthChi + 12) % 12;

  static const TrucInfo kTrucs[12] = {
      {"Kiến", "Cát", "Xuất hành, nhậm chức, đính hôn, khởi công", "Động thổ, phá dỡ, kiện tụng"},
      {"Trừ", "Thứ Cát", "Tẩy trần, chữa bệnh, giải trừ việc xấu, cắt tóc", "Cưới gả, ký hợp đồng lớn"},
      {"Mãn", "Đại Cát", "Cầu tài lộc, khai trương, xuất kho, may áo mới", "Kiện tụng, an táng, tranh chấp"},
      {"Bình", "Bình", "Sửa nhà cửa, san nền, giao dịch buôn bán", "Khởi công việc lớn mạo hiểm"},
      {"Định", "Đại Cát", "Đính hôn, cưới gả, ký kết, mua bán nhà đất", "Kiện tụng, xuất hành xa"},
      {"Chấp", "Cát", "Xây dựng, lập ước, tích trữ của cải, học tập", "Xuất tiền của lớn, dời nhà"},
      {"Phá", "Hung", "Phá dỡ nhà cũ, trừ bỏ điều xấu, chữa bệnh", "Cưới hỏi, khai trương, hội họp"},
      {"Nguy", "Bình", "Làm từ thiện, cúng tế, cầu an, đọc sách", "Đi thuyền, leo núi, mạo hiểm"},
      {"Thành", "Đại Cát", "Khai trương, nhập học, cưới hỏi, xuất hành tốt", "Tranh chấp, kiện cáo, cãi vã"},
      {"Thâu", "Cát", "Thu nợ, tích lũy tài sản, thu hoạch lộc lá", "Cho vay mượn tiền, mai táng"},
      {"Khai", "Đại Cát", "Khai trương, mở tiệm, kết hôn, thi cử, học hành", "Động thổ, chôn cất"},
      {"Bế", "Bình", "Tu sửa rào giậu, đắp đập, tiết kiệm, an định", "Khai trương, xuất hành, tháo dỡ"}
  };

  return kTrucs[trucIndex % 12];
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
    activityManager.goToPlugins(false);
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
      activityManager.goToPlugins(false);
      return;
    }

    // 2. Chuyển Tab Xem: [ Tháng ] và [ Ngày ]
    const int tabW = 76;
    const int tabGap = 6;
    const int totalTabsW = tabW * 2 + tabGap;
    const int tab1X = (screenWidth - totalTabsW) / 2;
    const int tab2X = tab1X + tabW + tabGap;
    if (touchY >= 6 && touchY <= 46) {
      if (touchX >= tab1X - 4 && touchX <= tab1X + tabW + 2) {
        setViewMode(CalendarViewMode::MONTH);
        return;
      }
      if (touchX >= tab2X - 2 && touchX <= tab2X + tabW + 4) {
        setViewMode(CalendarViewMode::DAY);
        return;
      }
    }

    // 3. Nút [ Hôm nay ] góc phải trên
    const int todayBtnW = 92;
    const int todayBtnX = screenWidth - todayBtnW - 12;
    if (touchX >= todayBtnX - 4 && touchY <= 48) {
      goToToday();
      return;
    }

    // --- XỬ LÝ CHẠM TRONG CHẾ ĐỘ THÁNG ---
    if (viewMode == CalendarViewMode::MONTH) {
      // Nút điều hướng tháng < (trái) và > (phải)
      if (touchY >= 56 && touchY <= 96) {
        if (touchX >= 10 && touchX <= 60) {
          prevMonth();
          return;
        }
        if (touchX >= screenWidth - 60 && touchX <= screenWidth - 10) {
          nextMonth();
          return;
        }
      }

      // Chạm vào các ô trong lưới tháng
      const int gridX = 14;
      const int gridY = 126;
      const int gridW = screenWidth - (gridX * 2);
      const int rowH = 46;

      if (touchX >= gridX && touchX <= gridX + gridW && touchY >= gridY && touchY <= gridY + rowH * 6) {
        int col = ((touchX - gridX) * 7) / gridW;
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
      const int firstDow = LunarCalendar::dayOfWeek(1, displayedMonth, displayedYear);
      const int maxDays = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
      int totalRows = (firstDow + maxDays + 6) / 7;
      if (totalRows < 5) totalRows = 5;
      const int cardY = gridY + totalRows * rowH + 6;
      if (touchY >= cardY) {
        setViewMode(CalendarViewMode::DAY);
        return;
      }
    }

    // --- XỬ LÝ CHẠM TRONG CHẾ ĐỘ NGÀY ---
    if (viewMode == CalendarViewMode::DAY) {
      // Nút chuyển ngày dưới đáy màn hình
      if (touchY >= 710 && touchY <= 780) {
        if (touchX >= 30 && touchX <= 180) {
          prevDay();
          return;
        }
        if (touchX >= 180 && touchX <= 300) {
          goToToday();
          return;
        }
        if (touchX >= 300 && touchX <= 450) {
          nextDay();
          return;
        }
      }
    }
  }
}

void CalendarAppActivity::renderHeader() {
  const int screenWidth = renderer.getScreenWidth();
  const int headerH = 50;

  // Đường kẻ mỏng 1px dưới header
  renderer.drawLine(0, headerH, screenWidth - 1, headerH, true);

  // Nút Back chuẩn hóa Squircle đồng nhất toàn hệ điều hành (cao 34px, Y=8, căn giữa header)
  TouchHeaderBackButton::drawSquircleBackButton(renderer, 12, 8, 34, 8);

  // Segmented Control [ Tháng ] | [ Ngày ] căn giữa thanh lịch (cao 34px, Y=8)
  const int segY = 8;
  const int segH = 34;
  const int tabW = 76;
  const int tabGap = 6;
  const int totalTabsW = tabW * 2 + tabGap;
  const int tab1X = (screenWidth - totalTabsW) / 2;
  const int tab2X = tab1X + tabW + tabGap;

  // Tab Tháng
  if (viewMode == CalendarViewMode::MONTH) {
    renderer.fillRoundedRect(tab1X, segY, tabW, segH, 8, Color::Black);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Tháng", EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, tab1X + (tabW - tw) / 2, segY + 7, "Tháng", false, EpdFontFamily::BOLD);
  } else {
    renderer.drawRoundedRect(tab1X, segY, tabW, segH, 1, 8, true);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Tháng", EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, tab1X + (tabW - tw) / 2, segY + 7, "Tháng", true, EpdFontFamily::REGULAR);
  }

  // Tab Ngày
  if (viewMode == CalendarViewMode::DAY) {
    renderer.fillRoundedRect(tab2X, segY, tabW, segH, 8, Color::Black);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Ngày", EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, tab2X + (tabW - tw) / 2, segY + 7, "Ngày", false, EpdFontFamily::BOLD);
  } else {
    renderer.drawRoundedRect(tab2X, segY, tabW, segH, 1, 8, true);
    int tw = renderer.getTextWidth(UI_10_FONT_ID, "Ngày", EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, tab2X + (tabW - tw) / 2, segY + 7, "Ngày", true, EpdFontFamily::REGULAR);
  }

  // Nút [ Hôm nay ] góc phải trên (Rộng 92px, cao 34px, Y=8, padding rộng rãi không bao giờ cấn chữ)
  const int todayBtnW = 92;
  const int todayBtnH = 34;
  const int todayBtnX = screenWidth - todayBtnW - 12;
  renderer.drawRoundedRect(todayBtnX, segY, todayBtnW, todayBtnH, 1, 8, true);
  int ttw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm nay", EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, todayBtnX + (todayBtnW - ttw) / 2, segY + 7, "Hôm nay", true, EpdFontFamily::REGULAR);
}

void CalendarAppActivity::renderMonthView() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  // 1. Thanh tiêu đề Tháng / Năm cân đối 2 bên
  const int titleBarY = 60;
  // Nút icon chevron vector tháng trước (bên trái)
  TouchHeaderBackButton::drawSquircleChevronButton(renderer, 14, titleBarY, 40, 32, /*isRight=*/false, 8);

  // Tiêu đề tháng năm căn giữa hoàn hảo
  char monthTitle[48];
  snprintf(monthTitle, sizeof(monthTitle), "Tháng %d, %d", displayedMonth, displayedYear);
  int titleTw = renderer.getTextWidth(NOTOSANS_14_FONT_ID, monthTitle, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_14_FONT_ID, (screenWidth - titleTw) / 2, titleBarY + 5, monthTitle, true,
                    EpdFontFamily::BOLD);

  // Nút icon chevron vector tháng sau (bên phải đối xứng)
  int nextBtnX = screenWidth - 14 - 40;
  TouchHeaderBackButton::drawSquircleChevronButton(renderer, nextBtnX, titleBarY, 40, 32, /*isRight=*/true, 8);

  // 2. Thanh tiêu đề Thứ (Thứ Hai -> Chủ Nhật)
  const int gridX = 14;
  const int gridW = screenWidth - (gridX * 2);
  const int dowY = 98;
  static const char* const kDows[] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};

  auto getColLeft = [gridX, gridW](int col) { return gridX + (col * gridW) / 7; };
  auto getColRight = [gridX, gridW](int col) { return gridX + ((col + 1) * gridW) / 7; };

  for (int i = 0; i < 7; ++i) {
    int colCenter = (getColLeft(i) + getColRight(i)) / 2;
    int tw = renderer.getTextWidth(UI_10_FONT_ID, kDows[i], EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, colCenter - (tw / 2), dowY, kDows[i], true, EpdFontFamily::BOLD);
  }
  // Đường kẻ phân cách hàng thứ cách đáy chữ thoáng đãng
  renderer.drawLine(gridX, dowY + 27, gridX + gridW, dowY + 27, true);

  // 3. Lưới Tháng: Số Dương vừa vặn thanh lịch, Số Âm nhỏ gọn trang nhã
  const int gridY = 130;
  const int rowH = 46;
  const int daysInMonth = LunarCalendar::daysInSolarMonth(displayedMonth, displayedYear);
  const int firstDow = LunarCalendar::dayOfWeek(1, displayedMonth, displayedYear);

  int totalRows = (firstDow + daysInMonth + 6) / 7;
  if (totalRows < 5) totalRows = 5;

  for (int dayVal = 1; dayVal <= daysInMonth; ++dayVal) {
    int slot = firstDow + dayVal - 1;
    int col = slot % 7;
    int row = slot / 7;

    int cellX = getColLeft(col);
    int cellW = getColRight(col) - cellX;
    int cellY = gridY + row * rowH;

    bool isToday = (dayVal == currentDay && displayedMonth == currentMonth && displayedYear == currentYear);
    bool isSelected = (dayVal == selectedDay);

    LunarDate lDate = LunarCalendar::solarToLunar(dayVal, displayedMonth, displayedYear);

    // Vẽ ô ngày: Nền đen chữ trắng cho ngày chọn hoặc viền đôi cho hôm nay
    if (isSelected) {
      renderer.fillRoundedRect(cellX + 2, cellY + 2, cellW - 4, rowH - 4, 8, Color::Black);
    } else if (isToday) {
      renderer.drawRoundedRect(cellX + 2, cellY + 2, cellW - 4, rowH - 4, 2, 8, true);
    }

    // Ngày Dương lịch (Font UI_12 BOLD)
    char solarStr[8];
    snprintf(solarStr, sizeof(solarStr), "%d", dayVal);
    int sTw = renderer.getTextWidth(UI_12_FONT_ID, solarStr, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, cellX + (cellW - sTw) / 2, cellY + 5, solarStr, !isSelected,
                      EpdFontFamily::BOLD);

    // Ngày Âm lịch (Font SMALL_FONT_ID)
    char lunarStr[16];
    if (lDate.day == 1) {
      snprintf(lunarStr, sizeof(lunarStr), "1/%d", lDate.month);
    } else {
      snprintf(lunarStr, sizeof(lunarStr), "%d", lDate.day);
    }

    bool isSpecialLunar = (lDate.day == 1 || lDate.day == 15 || lDate.lunarHoliday != nullptr);
    int lTw = renderer.getTextWidth(SMALL_FONT_ID, lunarStr, isSpecialLunar ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, cellX + (cellW - lTw) / 2, cellY + 25, lunarStr, !isSelected,
                      isSpecialLunar ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    // Chấm nhỏ báo ngày lễ
    if (lDate.lunarHoliday != nullptr || lDate.solarHoliday != nullptr) {
      renderer.fillRoundedRect(cellX + (cellW / 2) - 2, cellY + rowH - 6, 4, 3, 1,
                               isSelected ? Color::White : Color::Black);
    }
  }

  // 4. Thẻ Chi Tiết Ngày Được Chọn Ở Dưới Lưới Tháng (Card Thông Tin Liền Khối Đầy Đặn)
  const int cardY = gridY + totalRows * rowH + 6;
  const int cardW = screenWidth - (gridX * 2);
  const int cardH = screenHeight - cardY - 14;

  renderer.drawRoundedRect(gridX, cardY, cardW, cardH, 1, 14, true);

  LunarDate selLunar = LunarCalendar::solarToLunar(selectedDay, displayedMonth, displayedYear);
  const char* dowName = LunarCalendar::dayOfWeekName(selectedDay, displayedMonth, displayedYear);
  const char* solarTerm = LunarCalendar::getSolarTerm(selectedDay, displayedMonth, displayedYear);

  // Tiêu đề Dương Lịch: Thứ, ngày tháng năm (FONT UI_12 BOLD)
  char selSolarBuf[96];
  snprintf(selSolarBuf, sizeof(selSolarBuf), "%s, ngày %d/%d/%d", dowName, selectedDay, displayedMonth, displayedYear);
  renderer.drawText(UI_12_FONT_ID, gridX + 16, cardY + 12, selSolarBuf, true, EpdFontFamily::BOLD);

  // Ngày Lễ (nếu có) hiển thị bên cạnh cùng dòng
  const char* holiday = selLunar.lunarHoliday ? selLunar.lunarHoliday : selLunar.solarHoliday;
  if (holiday) {
    char holStr[64];
    snprintf(holStr, sizeof(holStr), "★ %s", holiday);
    int holTw = renderer.getTextWidth(UI_10_FONT_ID, holStr, EpdFontFamily::BOLD);
    int holX = gridX + cardW - holTw - 16;
    int solarTw = renderer.getTextWidth(UI_12_FONT_ID, selSolarBuf, EpdFontFamily::BOLD);
    if (holX > gridX + 16 + solarTw + 12) {
      renderer.drawText(UI_10_FONT_ID, holX, cardY + 14, holStr, true, EpdFontFamily::BOLD);
    }
  }

  // Dòng 1: Âm Lịch (Ngày, Tháng Âm và Năm Can Chi)
  char selLunarBuf[96];
  if (selLunar.isLeap) {
    snprintf(selLunarBuf, sizeof(selLunarBuf), "Âm lịch: Ngày %d/%d (Nhuận) · Năm %s", selLunar.day, selLunar.month,
             selLunar.yearCanChi.c_str());
  } else {
    snprintf(selLunarBuf, sizeof(selLunarBuf), "Âm lịch: Ngày %d/%d · Năm %s", selLunar.day, selLunar.month,
             selLunar.yearCanChi.c_str());
  }
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 38, selLunarBuf, true, EpdFontFamily::REGULAR);

  // Dòng 2: Tiết Khí và Can Chi Ngày (Tách riêng dòng không bao giờ tràn lề)
  char termAndDayBuf[96];
  snprintf(termAndDayBuf, sizeof(termAndDayBuf), "Tiết Khí: %s · Can Chi: Ngày %s", solarTerm, selLunar.dayCanChi.c_str());
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 60, termAndDayBuf, true, EpdFontFamily::REGULAR);

  // ĐƯỜNG KẺ DUY NHẤT PHÂN CÁCH (Khoảng cách từ dòng trên đến đường kẻ là 14px thoáng đãng)
  renderer.drawLine(gridX + 16, cardY + 86, gridX + cardW - 16, cardY + 86, true);

  // Dòng 3: Trực & việc nên làm
  TrucInfo tc = getTrucInfo(selectedDay, displayedMonth, displayedYear, selLunar);
  char tcSummary[144];
  snprintf(tcSummary, sizeof(tcSummary), "Trực %s (%s) · Việc nên: %s", tc.name, tc.nature, tc.favorable);
  if (renderer.getTextWidth(UI_10_FONT_ID, tcSummary, EpdFontFamily::REGULAR) > cardW - 32) {
    snprintf(tcSummary, sizeof(tcSummary), "Trực: %s (%s) · Nên: %s", tc.name, tc.nature, tc.favorable);
    if (renderer.getTextWidth(UI_10_FONT_ID, tcSummary, EpdFontFamily::REGULAR) > cardW - 32) {
      char* comma = strrchr(tcSummary, ',');
      if (comma) *comma = '\0';
    }
  }
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 100, tcSummary, true, EpdFontFamily::REGULAR);

  // Dòng 4: Hướng xuất hành dân gian (Không có đường kẻ đè chữ bên dưới)
  const char* dirStr = getDirectionInfo(selLunar.dayCanChi);
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 124, dirStr, true, EpdFontFamily::REGULAR);

  // Dòng 5: Tiêu đề Giờ Hoàng Đạo
  renderer.drawText(UI_10_FONT_ID, gridX + 16, cardY + 150, "GIỜ HOÀNG ĐẠO (GIỜ TỐT):", true, EpdFontFamily::BOLD);

  // 6 Giờ Hoàng Đạo (Giờ tốt) trình bày 2 cột tinh tế, ngay ngắn
  HoangDaoHourInfo hd = getHoangDaoDetails(selectedDay, displayedMonth, displayedYear);
  const int col1X = gridX + 20;
  const int col2X = gridX + (cardW / 2) + 8;
  for (int i = 0; i < 3; ++i) {
    char h1[64], h2[64];
    snprintf(h1, sizeof(h1), "• %s", hd.col1[i]);
    snprintf(h2, sizeof(h2), "• %s", hd.col2[i]);
    renderer.drawText(UI_10_FONT_ID, col1X, cardY + 172 + i * 20, h1, true, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, col2X, cardY + 172 + i * 20, h2, true, EpdFontFamily::REGULAR);
  }

  // Nút gợi ý: [ Xem Tờ Lịch Bóc Chi Tiết > ] căn giữa hoàn hảo cả chữ và icon chevron
  const int openDayBtnW = 310;
  const int openDayBtnH = 36;
  const int openDayBtnX = (screenWidth - openDayBtnW) / 2;
  const int openDayBtnY = cardY + cardH - 46;

  renderer.drawRoundedRect(openDayBtnX, openDayBtnY, openDayBtnW, openDayBtnH, 1, 18, true);

  const char* openDayText = "Xem Tờ Lịch Bóc Chi Tiết";
  int odTw = renderer.getTextWidth(UI_10_FONT_ID, openDayText, EpdFontFamily::BOLD);
  const int iconGap = 10;
  const int totalContentW = odTw + iconGap + 6;
  const int textStartX = openDayBtnX + (openDayBtnW - totalContentW) / 2;

  renderer.drawText(UI_10_FONT_ID, textStartX, openDayBtnY + 9, openDayText, true, EpdFontFamily::BOLD);

  // Vẽ Chevron vector `>` thanh mảnh, căn giữa theo trục Y của nút
  const int cx = textStartX + odTw + iconGap + 2;
  const int cy = openDayBtnY + openDayBtnH / 2;
  renderer.drawLine(cx - 3, cy - 5, cx + 2, cy, 2, true);
  renderer.drawLine(cx + 2, cy, cx - 3, cy + 5, 2, true);
}

void CalendarAppActivity::renderDayView() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();

  LunarDate selLunar = LunarCalendar::solarToLunar(selectedDay, displayedMonth, displayedYear);
  const char* dowName = LunarCalendar::dayOfWeekName(selectedDay, displayedMonth, displayedYear);

  // === TỜ LỊCH BÓC TRUYỀN THỐNG VIỆT NAM (BLOC LỊCH) TRÊN MÀN HÌNH DỌC ===
  const int cardX = 16;
  const int cardY = 56;
  const int cardW = screenWidth - (cardX * 2);
  const int cardH = screenHeight - cardY - 14;
  const int cardCenterX = cardX + (cardW / 2);

  // Khung ngoài duy nhất bo góc 16px, viền 1px nét mảnh
  renderer.drawRoundedRect(cardX, cardY, cardW, cardH, 1, 16, true);

  // 1. GÁY NẸP LỊCH PHÍA TRÊN
  const int topHeaderH = 34;
  renderer.fillRoundedRect(cardX + 1, cardY + 1, cardW - 2, topHeaderH, 15, Color::Black);
  renderer.fillRect(cardX + 1, cardY + topHeaderH - 8, cardW - 2, 8, Color::Black);

  const char* blocTitle = "LỊCH VẠN NIÊN · VIỆT NAM";
  int btTw = renderer.getTextWidth(UI_10_FONT_ID, blocTitle, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, cardCenterX - (btTw / 2), cardY + 8, blocTitle, false, EpdFontFamily::BOLD);

  // 2. THỨ TRONG TUẦN (CĂN GIỮA)
  char upperDow[32];
  snprintf(upperDow, sizeof(upperDow), "%s", dowName);
  for (int i = 0; upperDow[i]; ++i) upperDow[i] = (char)toupper((unsigned char)upperDow[i]);
  int dowTw = renderer.getTextWidth(UI_12_FONT_ID, upperDow, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, cardCenterX - (dowTw / 2), cardY + 44, upperDow, true,
                    EpdFontFamily::BOLD);

  // 3. THÁNG & NĂM DƯƠNG LỊCH (CĂN GIỮA)
  char solarMonthYear[48];
  snprintf(solarMonthYear, sizeof(solarMonthYear), "Tháng %d năm %d", displayedMonth, displayedYear);
  int smTw = renderer.getTextWidth(UI_10_FONT_ID, solarMonthYear, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardCenterX - (smTw / 2), cardY + 76, solarMonthYear, true,
                    EpdFontFamily::REGULAR);

  // 4. CON SỐ NGÀY DƯƠNG LỊCH (Ubuntu-Bold 74pt ~53px cao, CĂN GIỮA TUYỆT ĐỐI)
  char dayStr[8];
  snprintf(dayStr, sizeof(dayStr), "%d", selectedDay);
  clock_font::drawClock(renderer, cardCenterX, cardY + 160, dayStr);

  // 5. ĐƯỜNG KẺ PHÂN CÁCH TRANG NHÃ
  renderer.drawLine(cardX + 24, cardY + 172, cardX + cardW - 24, cardY + 172, true);

  // 6. NGÀY THÁNG ÂM LỊCH (CHỮ TRANG NHÃ, KHÔNG DÙNG HỘP VIỀN VUÔNG RỐI MẮT)
  char lunarDayStr[64];
  if (selLunar.isLeap) {
    snprintf(lunarDayStr, sizeof(lunarDayStr), "Ngày %d Tháng %d (Nhuận) Âm Lịch", selLunar.day, selLunar.month);
  } else {
    snprintf(lunarDayStr, sizeof(lunarDayStr), "Ngày %d Tháng %d Âm Lịch", selLunar.day, selLunar.month);
  }
  int ldTw = renderer.getTextWidth(UI_12_FONT_ID, lunarDayStr, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, cardCenterX - (ldTw / 2), cardY + 184, lunarDayStr, true,
                    EpdFontFamily::BOLD);

  char lunarSubStr[64];
  const char* solarTerm = LunarCalendar::getSolarTerm(selectedDay, displayedMonth, displayedYear);
  snprintf(lunarSubStr, sizeof(lunarSubStr), "Năm %s  ·  Tiết Khí: %s", selLunar.yearCanChi.c_str(), solarTerm);
  int lyTw = renderer.getTextWidth(UI_10_FONT_ID, lunarSubStr, EpdFontFamily::REGULAR);
  renderer.drawText(UI_10_FONT_ID, cardCenterX - (lyTw / 2), cardY + 214, lunarSubStr, true,
                    EpdFontFamily::REGULAR);

  // 7. TAM TRỤ CAN CHI (THANH CARD 3 CỘT LIỀN KHỐI TRANG NHÃ)
  const int canChiY = cardY + 244;
  const int canChiW = cardW - 36;
  const int canChiX = cardX + 18;
  const int canChiH = 46;
  renderer.drawRoundedRect(canChiX, canChiY, canChiW, canChiH, 1, 8, true);

  const int colW = canChiW / 3;
  renderer.drawLine(canChiX + colW, canChiY + 4, canChiX + colW, canChiY + canChiH - 4, true);
  renderer.drawLine(canChiX + colW * 2, canChiY + 4, canChiX + colW * 2, canChiY + canChiH - 4, true);

  const char* titles[3] = {"NGÀY", "THÁNG", "NĂM"};
  const std::string vals[3] = {selLunar.dayCanChi, selLunar.monthCanChi, selLunar.yearCanChi};

  for (int i = 0; i < 3; ++i) {
    int curColX = canChiX + i * colW;
    int tTw = renderer.getTextWidth(SMALL_FONT_ID, titles[i], EpdFontFamily::REGULAR);
    renderer.drawText(SMALL_FONT_ID, curColX + (colW - tTw) / 2, canChiY + 5, titles[i], true, EpdFontFamily::REGULAR);

    int vTw = renderer.getTextWidth(UI_10_FONT_ID, vals[i].c_str(), EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, curColX + (colW - vTw) / 2, canChiY + 22, vals[i].c_str(), true, EpdFontFamily::BOLD);
  }

  // 8. ĐƯỜNG KẺ PHÂN CÁCH TRANG NHÃ
  renderer.drawLine(cardX + 24, cardY + 300, cardX + cardW - 24, cardY + 300, true);

  // 9. GIỜ HOÀNG ĐẠO (GIỜ TỐT)
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 312, "GIỜ HOÀNG ĐẠO (GIỜ TỐT):", true, EpdFontFamily::BOLD);

  HoangDaoHourInfo hd = getHoangDaoDetails(selectedDay, displayedMonth, displayedYear);
  const int hdColW = (cardW - 48) / 2;
  for (int i = 0; i < 3; ++i) {
    char item1[64], item2[64];
    snprintf(item1, sizeof(item1), "• %s", hd.col1[i]);
    snprintf(item2, sizeof(item2), "• %s", hd.col2[i]);
    renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 334 + i * 22, item1, true, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, cardX + 24 + hdColW, cardY + 334 + i * 22, item2, true, EpdFontFamily::REGULAR);
  }

  // 10. ĐƯỜNG KẺ PHÂN CÁCH TRANG NHÃ
  renderer.drawLine(cardX + 24, cardY + 410, cardX + cardW - 24, cardY + 410, true);

  // 11. HƯỚNG XUẤT HÀNH
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 422, "HƯỚNG XUẤT HÀNH DÂN GIAN:", true, EpdFontFamily::BOLD);
  const char* dirStr = getDirectionInfo(selLunar.dayCanChi);
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 444, dirStr, true, EpdFontFamily::REGULAR);

  // 12. ĐƯỜNG KẺ PHÂN CÁCH TRANG NHÃ
  renderer.drawLine(cardX + 24, cardY + 472, cardX + cardW - 24, cardY + 472, true);

  // 13. VIỆC NÊN LÀM & KIÊNG KỴ (THEO 12 TRỰC CHUẨN THIÊN VĂN LỊCH VẠN NIÊN)
  TrucInfo tc = getTrucInfo(selectedDay, displayedMonth, displayedYear, selLunar);
  char trucHeader[64];
  snprintf(trucHeader, sizeof(trucHeader), "TRỰC %s (%s) - VIỆC NÊN LÀM:", tc.name, tc.nature);
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 482, trucHeader, true, EpdFontFamily::BOLD);
  char shouldDo[128];
  snprintf(shouldDo, sizeof(shouldDo), "• Nên: %s.", tc.favorable);
  if (renderer.getTextWidth(UI_10_FONT_ID, shouldDo, EpdFontFamily::REGULAR) > cardW - 48) {
    snprintf(shouldDo, sizeof(shouldDo), "• Nên: %s", tc.favorable);
    char* comma = strrchr(shouldDo, ',');
    if (comma) *comma = '\0';
  }
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 502, shouldDo, true, EpdFontFamily::REGULAR);
  char shouldAvoid[128];
  snprintf(shouldAvoid, sizeof(shouldAvoid), "• Kỵ: %s.", tc.unfavorable);
  if (renderer.getTextWidth(UI_10_FONT_ID, shouldAvoid, EpdFontFamily::REGULAR) > cardW - 48) {
    snprintf(shouldAvoid, sizeof(shouldAvoid), "• Kỵ: %s", tc.unfavorable);
    char* comma = strrchr(shouldAvoid, ',');
    if (comma) *comma = '\0';
  }
  renderer.drawText(UI_10_FONT_ID, cardX + 24, cardY + 522, shouldAvoid, true, EpdFontFamily::REGULAR);

  // 14. THANH ĐIỀU HƯỚNG DƯỚI ĐÁY TỜ LỊCH (Nút Squircle tích hợp Chevron Vector căn giữa tuyệt đối)
  const int navY = cardY + cardH - 52;
  const int navBtnH = 38;
  const int prevW = 126;
  const int todayW = 104;
  const int nextW = 126;
  const int gap = 12;
  const int totalNavW = prevW + todayW + nextW + gap * 2;
  const int startNavX = (screenWidth - totalNavW) / 2;

  // Nút Hôm trước: Squircle viền mảnh kèm Chevron trái căn giữa
  const int prevX = startNavX;
  renderer.fillRoundedRect(prevX, navY, prevW, navBtnH, 10, Color::White);
  renderer.drawRoundedRect(prevX, navY, prevW, navBtnH, 1, 10, true);
  int ptw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm trước", EpdFontFamily::BOLD);
  const int prevContentW = ptw + 14;
  const int prevStartX = prevX + (prevW - prevContentW) / 2;
  // Chevron trái nét vector
  const int cxP = prevStartX + 4;
  const int cyP = navY + navBtnH / 2;
  renderer.drawLine(cxP + 3, cyP - 5, cxP - 2, cyP, 2, true);
  renderer.drawLine(cxP - 2, cyP, cxP + 3, cyP + 5, 2, true);
  renderer.drawText(UI_10_FONT_ID, prevStartX + 14, navY + 10, "Hôm trước", true, EpdFontFamily::BOLD);

  // Nút Hôm nay căn giữa
  const int todayX = prevX + prevW + gap;
  renderer.fillRoundedRect(todayX, navY, todayW, navBtnH, 10, Color::White);
  renderer.drawRoundedRect(todayX, navY, todayW, navBtnH, 1, 10, true);
  int ttw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm nay", EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, todayX + (todayW - ttw) / 2, navY + 10, "Hôm nay", true, EpdFontFamily::BOLD);

  // Nút Hôm sau: Squircle viền mảnh kèm Chevron phải căn giữa
  const int nextX = todayX + todayW + gap;
  renderer.fillRoundedRect(nextX, navY, nextW, navBtnH, 10, Color::White);
  renderer.drawRoundedRect(nextX, navY, nextW, navBtnH, 1, 10, true);
  int ntw = renderer.getTextWidth(UI_10_FONT_ID, "Hôm sau", EpdFontFamily::BOLD);
  const int nextContentW = ntw + 14;
  const int nextStartX = nextX + (nextW - nextContentW) / 2;
  renderer.drawText(UI_10_FONT_ID, nextStartX, navY + 10, "Hôm sau", true, EpdFontFamily::BOLD);
  // Chevron phải nét vector
  const int cxN = nextStartX + ntw + 10;
  const int cyN = navY + navBtnH / 2;
  renderer.drawLine(cxN - 3, cyN - 5, cxN + 2, cyN, 2, true);
  renderer.drawLine(cxN + 2, cyN, cxN - 3, cyN + 5, 2, true);
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
