#pragma once

#include <cstdint>
#include <string>

struct LunarDate {
  int day = 1;
  int month = 1;
  int year = 2026;
  bool isLeap = false;
  std::string yearCanChi;   // e.g. "Bính Ngọ", "Giáp Thìn"
  std::string dayCanChi;    // e.g. "Canh Tuất"
  std::string monthCanChi;  // e.g. "Mậu Tuất"
  const char* solarHoliday = nullptr;
  const char* lunarHoliday = nullptr;
  bool isSpecialDay = false;  // Rằm (15) hoặc Mùng 1
};

namespace LunarCalendar {

// Chuyển đổi ngày Dương lịch sang Âm lịch (thuật toán Hồ Ngọc Đức, múi giờ GMT+7)
LunarDate solarToLunar(int day, int month, int year, double timeZone = 7.0);

// Lấy ngày lễ Dương lịch và Âm lịch Việt Nam
const char* getSolarHoliday(int day, int month);
const char* getLunarHoliday(int day, int month, bool isLeap = false);

// Can Chi Helpers
std::string getYearCanChi(int lunarYear);
std::string getDayCanChi(int day, int month, int year);
std::string getMonthCanChi(int lunarMonth, int lunarYear);

// Định dạng chuỗi hiển thị
std::string formatShort(const LunarDate& l);    // e.g. "14/9 AL"
std::string formatFull(const LunarDate& l);     // e.g. "14/09 Âm lịch (Bính Ngọ)"
std::string formatDetailed(const LunarDate& l); // e.g. "Ngày 14 tháng 9 năm Bính Ngọ"

// Số ngày trong tháng Dương lịch
int daysInSolarMonth(int month, int year);

// Ngày trong tuần (0 = Thứ Hai, ..., 6 = Chủ Nhật)
int dayOfWeek(int day, int month, int year);
const char* dayOfWeekName(int day, int month, int year);

// 24 Tiết Khí truyền thống theo kinh độ Mặt Trời
const char* getSolarTerm(int day, int month, int year);

}  // namespace LunarCalendar
