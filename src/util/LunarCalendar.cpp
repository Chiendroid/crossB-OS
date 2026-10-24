#include "LunarCalendar.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr double PI = 3.14159265358979323846;

static const char* const kCan[] = {"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"};
static const char* const kChi[] = {"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"};

static const char* const kDayNames[] = {
    "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy", "Chủ Nhật"
};

int jdFromDate(int dd, int mm, int yy) {
  int a = (14 - mm) / 12;
  int y = yy + 4800 - a;
  int m = mm + 12 * a - 3;
  int jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
  if (jd < 2299161) {
    jd = dd + (153 * m + 2) / 5 + 365 * y + y / 4 - 32083;
  }
  return jd;
}

int getNewMoonDay(int k, double timeZone = 7.0) {
  double T = k / 1236.85;
  double T2 = T * T;
  double T3 = T2 * T;
  double dr = PI / 180.0;
  double Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  Jd1 += 0.00033 * std::sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);

  double M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  double Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  double F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;

  double C1 = (0.1734 - 0.000393 * T) * std::sin(M * dr) + 0.0021 * std::sin(2 * M * dr);
  C1 -= 0.4068 * std::sin(Mpr * dr) + 0.0161 * std::sin(2 * Mpr * dr);
  C1 -= 0.0004 * std::sin(3 * Mpr * dr);
  C1 += 0.0104 * std::sin(2 * F * dr) - 0.0051 * std::sin((M + Mpr) * dr);
  C1 -= 0.0074 * std::sin((M - Mpr) * dr) + 0.0004 * std::sin((2 * F + M) * dr);
  C1 -= 0.0004 * std::sin((2 * F - M) * dr) - 0.0006 * std::sin((2 * F + Mpr) * dr);
  C1 += 0.0010 * std::sin((2 * F - Mpr) * dr) + 0.0005 * std::sin((M + 2 * Mpr) * dr);

  double deltat = 0.0;
  if (T < -11.0) {
    deltat = 0.001 + 0.000839 * T + 0.0002261 * T2 - 0.00000845 * T3 - 0.000000081 * T * T3;
  } else {
    deltat = -0.0000278 + 0.000265 * T + 0.000262 * T2;
  }
  double JdNew = Jd1 + C1 - deltat;
  return static_cast<int>(std::floor(JdNew + 0.5 + timeZone / 24.0));
}

int getSunLongitude(int jdn, double timeZone = 7.0) {
  double T = (jdn - 2451545.5 - timeZone / 24.0) / 36525.0;
  double T2 = T * T;
  double dr = PI / 180.0;
  double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(M * dr);
  DL += (0.019993 - 0.000101 * T) * std::sin(2 * M * dr) + 0.000290 * std::sin(3 * M * dr);
  double L = (L0 + DL) * dr;
  L = L - 2 * PI * std::floor(L / (2 * PI));
  return static_cast<int>(std::floor(L / (PI / 6.0)));
}

int getLunarMonth11(int yy, double timeZone = 7.0) {
  int off = jdFromDate(31, 12, yy) - 2415021;
  int k = static_cast<int>(std::floor(off / 29.530588853));
  int nm = getNewMoonDay(k, timeZone);
  int sunLong = getSunLongitude(nm, timeZone);
  if (sunLong >= 9) {
    nm = getNewMoonDay(k - 1, timeZone);
  }
  return nm;
}

int getLeapMonthOffset(int a11, double timeZone = 7.0) {
  int k = static_cast<int>(std::floor((a11 - 2415021.076998695) / 29.530588853 + 0.5));
  int last = 0;
  int i = 1;
  int arc = getSunLongitude(getNewMoonDay(k + i, timeZone), timeZone);
  while (true) {
    last = arc;
    i++;
    arc = getSunLongitude(getNewMoonDay(k + i, timeZone), timeZone);
    if (arc == last || i >= 14) break;
  }
  return i - 1;
}

}  // namespace

namespace LunarCalendar {

LunarDate solarToLunar(int day, int month, int year, double timeZone) {
  LunarDate res;
  int dayNumber = jdFromDate(day, month, year);
  int k = static_cast<int>(std::floor((dayNumber - 2415021.076998695) / 29.530588853));
  int monthStart = getNewMoonDay(k + 1, timeZone);
  if (monthStart > dayNumber) {
    monthStart = getNewMoonDay(k, timeZone);
  } else {
    k++;
  }

  int a11 = getLunarMonth11(year, timeZone);
  int b11 = a11;
  int lunarYear = year;
  if (a11 >= monthStart) {
    lunarYear = year;
    a11 = getLunarMonth11(year - 1, timeZone);
  } else {
    lunarYear = year + 1;
    b11 = getLunarMonth11(year + 1, timeZone);
  }

  int lunarDay = dayNumber - monthStart + 1;
  int diff = static_cast<int>((monthStart - a11) / 29.0);
  bool lunarLeap = false;
  int lunarMonth = diff + 11;

  if (b11 - a11 > 365) {
    int leapMonthDiff = getLeapMonthOffset(a11, timeZone);
    if (diff >= leapMonthDiff) {
      lunarMonth = diff + 10;
      if (diff == leapMonthDiff) {
        lunarLeap = true;
      }
    }
  }

  if (lunarMonth > 12) {
    lunarMonth -= 12;
  }
  if (lunarMonth >= 11 && diff < 4) {
    lunarYear -= 1;
  }

  res.day = lunarDay;
  res.month = lunarMonth;
  res.year = lunarYear;
  res.isLeap = lunarLeap;
  res.isSpecialDay = (lunarDay == 1 || lunarDay == 15);

  // Can Chi
  res.yearCanChi = getYearCanChi(lunarYear);
  res.dayCanChi = getDayCanChi(day, month, year);
  res.monthCanChi = getMonthCanChi(lunarMonth, lunarYear);

  // Ngày lễ
  res.solarHoliday = getSolarHoliday(day, month);
  res.lunarHoliday = getLunarHoliday(lunarDay, lunarMonth, lunarLeap);

  return res;
}

std::string getYearCanChi(int lunarYear) {
  int canIdx = (lunarYear + 6) % 10;
  int chiIdx = (lunarYear + 8) % 12;
  if (canIdx < 0) canIdx += 10;
  if (chiIdx < 0) chiIdx += 12;
  return std::string(kCan[canIdx]) + " " + kChi[chiIdx];
}

std::string getDayCanChi(int day, int month, int year) {
  int jd = jdFromDate(day, month, year);
  int canIdx = (jd + 9) % 10;
  int chiIdx = (jd + 1) % 12;
  if (canIdx < 0) canIdx += 10;
  if (chiIdx < 0) chiIdx += 12;
  return std::string(kCan[canIdx]) + " " + kChi[chiIdx];
}

std::string getMonthCanChi(int lunarMonth, int lunarYear) {
  int yearCanIdx = (lunarYear + 6) % 10;
  if (yearCanIdx < 0) yearCanIdx += 10;
  int canIdx = (yearCanIdx * 2 + lunarMonth) % 10;
  int chiIdx = (lunarMonth + 1) % 12;
  return std::string(kCan[canIdx]) + " " + kChi[chiIdx];
}

const char* getSolarHoliday(int day, int month) {
  if (day == 1 && month == 1) return "Tết Dương Lịch";
  if (day == 14 && month == 2) return "Lễ Tình Nhân";
  if (day == 8 && month == 3) return "Quốc tế Phụ nữ";
  if (day == 30 && month == 4) return "Giải phóng Miền Nam";
  if (day == 1 && month == 5) return "Quốc tế Lao động";
  if (day == 1 && month == 6) return "Quốc tế Thiếu nhi";
  if (day == 27 && month == 7) return "Thương binh Liệt sĩ";
  if (day == 19 && month == 8) return "Cách mạng Tháng Tám";
  if (day == 2 && month == 9) return "Quốc khánh";
  if (day == 20 && month == 10) return "Phụ nữ Việt Nam";
  if (day == 20 && month == 11) return "Nhà giáo Việt Nam";
  if (day == 22 && month == 12) return "Quân đội Nhân dân";
  if (day == 25 && month == 12) return "Giáng sinh";
  return nullptr;
}

const char* getLunarHoliday(int day, int month, bool isLeap) {
  if (isLeap) return nullptr;
  if (day == 1 && month == 1) return "Mùng 1 Tết";
  if (day == 2 && month == 1) return "Mùng 2 Tết";
  if (day == 3 && month == 1) return "Mùng 3 Tết";
  if (day == 15 && month == 1) return "Tết Nguyên Tiêu";
  if (day == 3 && month == 3) return "Tết Hàn Thực";
  if (day == 10 && month == 3) return "Giỗ Tổ Hùng Vương";
  if (day == 15 && month == 4) return "Lễ Phật Đản";
  if (day == 5 && month == 5) return "Tết Đoan Ngọ";
  if (day == 15 && month == 7) return "Lễ Vu Lan";
  if (day == 15 && month == 8) return "Tết Trung Thu";
  if (day == 9 && month == 9) return "Tết Trùng Cửu";
  if (day == 10 && month == 10) return "Tết Thường Tân";
  if (day == 15 && month == 10) return "Tết Hạ Nguyên";
  if (day == 23 && month == 12) return "Ông Táo Chầu Trời";
  if ((day == 29 || day == 30) && month == 12) return "Giao Thừa";
  return nullptr;
}

std::string formatShort(const LunarDate& l) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%d/%d AL", l.day, l.month);
  return std::string(buf);
}

std::string formatFull(const LunarDate& l) {
  char buf[64];
  if (l.isLeap) {
    snprintf(buf, sizeof(buf), "%d/%d (Nhuận) Âm lịch · %s", l.day, l.month, l.yearCanChi.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%d/%d Âm lịch · %s", l.day, l.month, l.yearCanChi.c_str());
  }
  return std::string(buf);
}

std::string formatDetailed(const LunarDate& l) {
  char buf[96];
  if (l.isLeap) {
    snprintf(buf, sizeof(buf), "Ngày %d tháng %d (Nhuận) năm %s", l.day, l.month, l.yearCanChi.c_str());
  } else {
    snprintf(buf, sizeof(buf), "Ngày %d tháng %d năm %s", l.day, l.month, l.yearCanChi.c_str());
  }
  return std::string(buf);
}

int daysInSolarMonth(int month, int year) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  if (month == 2) {
    bool isLeap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return isLeap ? 29 : 28;
  }
  return kDays[month - 1];
}

int dayOfWeek(int day, int month, int year) {
  // Tomohiko Sakamoto's algorithm (0 = Chủ Nhật, 1 = Thứ Hai, ..., 6 = Thứ Bảy)
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) year -= 1;
  int w = (year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7;
  // Quy đổi về chuẩn ISO: 0 = Thứ Hai, 1 = Thứ Ba, ..., 6 = Chủ Nhật
  return (w + 6) % 7;
}

const char* dayOfWeekName(int day, int month, int year) {
  int dow = dayOfWeek(day, month, year);
  if (dow >= 0 && dow < 7) return kDayNames[dow];
  return "Thứ Hai";
}

const char* getSolarTerm(int day, int month, int year) {
  static const char* const kSolarTerms[24] = {
      "Xuân Phân", "Thanh Minh", "Cốc Vũ", "Lập Hạ", "Tiểu Mãn", "Mang Chủng",
      "Hạ Chí", "Tiểu Thử", "Đại Thử", "Lập Thu", "Xử Thử", "Bạch Lộ",
      "Thu Phân", "Hàn Lộ", "Sương Giáng", "Lập Đông", "Tiểu Tuyết", "Đại Tuyết",
      "Đông Chí", "Tiểu Hàn", "Đại Hàn", "Lập Xuân", "Vũ Thủy", "Kinh Trập"
  };
  int jdn = jdFromDate(day, month, year);
  double T = (jdn - 2451545.5 - 7.0 / 24.0) / 36525.0;
  double T2 = T * T;
  double dr = PI / 180.0;
  double M = 357.52910 + 35999.05030 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  double L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  double DL = (1.914600 - 0.004817 * T - 0.000014 * T2) * std::sin(M * dr);
  DL += (0.019993 - 0.000101 * T) * std::sin(2 * M * dr) + 0.000290 * std::sin(3 * M * dr);
  double L = (L0 + DL) * dr;
  L = L - 2 * PI * std::floor(L / (2 * PI));
  int termIdx = static_cast<int>(std::floor(L / (PI / 12.0))) % 24;
  if (termIdx < 0) termIdx += 24;
  return kSolarTerms[termIdx];
}

}  // namespace LunarCalendar
