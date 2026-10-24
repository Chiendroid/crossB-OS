#pragma once

#include <string>

#include "activities/Activity.h"
#include "util/LunarCalendar.h"

enum class CalendarViewMode {
  MONTH = 0,
  DAY = 1,
};

class CalendarAppActivity final : public Activity {
 public:
  CalendarAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void nextMonth();
  void prevMonth();
  void nextDay();
  void prevDay();
  void goToToday();
  void selectDay(int day);
  void setViewMode(CalendarViewMode mode);

  void renderHeader();
  void renderMonthView();
  void renderDayView();

  CalendarViewMode viewMode = CalendarViewMode::MONTH;

  int displayedYear = 2026;
  int displayedMonth = 10;
  int selectedDay = 23;

  int currentYear = 2026;
  int currentMonth = 10;
  int currentDay = 23;
};
