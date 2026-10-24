#include "VietnameseTelex.h"

#include <cctype>
#include <cstring>

namespace vietnamese_telex {

namespace {

struct CharMapping {
  const char* utf8;
  char base;
  VnMod mod;
  VnTone tone;
  bool isUpper;
};

// Bảng ánh xạ các ký tự tiếng Việt UTF-8 chuẩn
static const CharMapping MAPPINGS[] = {
    // === 'a' thường ===
    {"a", 'a', VnMod::None, VnTone::None, false},
    {"á", 'a', VnMod::None, VnTone::Acute, false},
    {"à", 'a', VnMod::None, VnTone::Grave, false},
    {"ả", 'a', VnMod::None, VnTone::Hook, false},
    {"ã", 'a', VnMod::None, VnTone::Tilde, false},
    {"ạ", 'a', VnMod::None, VnTone::Dot, false},

    {"ă", 'a', VnMod::HornOrBreve, VnTone::None, false},
    {"ắ", 'a', VnMod::HornOrBreve, VnTone::Acute, false},
    {"ằ", 'a', VnMod::HornOrBreve, VnTone::Grave, false},
    {"ẳ", 'a', VnMod::HornOrBreve, VnTone::Hook, false},
    {"ẵ", 'a', VnMod::HornOrBreve, VnTone::Tilde, false},
    {"ặ", 'a', VnMod::HornOrBreve, VnTone::Dot, false},

    {"â", 'a', VnMod::Hat, VnTone::None, false},
    {"ấ", 'a', VnMod::Hat, VnTone::Acute, false},
    {"ầ", 'a', VnMod::Hat, VnTone::Grave, false},
    {"ẩ", 'a', VnMod::Hat, VnTone::Hook, false},
    {"ẫ", 'a', VnMod::Hat, VnTone::Tilde, false},
    {"ậ", 'a', VnMod::Hat, VnTone::Dot, false},

    // === 'A' hoa ===
    {"A", 'a', VnMod::None, VnTone::None, true},
    {"Á", 'a', VnMod::None, VnTone::Acute, true},
    {"À", 'a', VnMod::None, VnTone::Grave, true},
    {"Ả", 'a', VnMod::None, VnTone::Hook, true},
    {"Ã", 'a', VnMod::None, VnTone::Tilde, true},
    {"Ạ", 'a', VnMod::None, VnTone::Dot, true},

    {"Ă", 'a', VnMod::HornOrBreve, VnTone::None, true},
    {"Ắ", 'a', VnMod::HornOrBreve, VnTone::Acute, true},
    {"Ằ", 'a', VnMod::HornOrBreve, VnTone::Grave, true},
    {"Ẳ", 'a', VnMod::HornOrBreve, VnTone::Hook, true},
    {"Ẵ", 'a', VnMod::HornOrBreve, VnTone::Tilde, true},
    {"Ặ", 'a', VnMod::HornOrBreve, VnTone::Dot, true},

    {"Â", 'a', VnMod::Hat, VnTone::None, true},
    {"Ấ", 'a', VnMod::Hat, VnTone::Acute, true},
    {"Ầ", 'a', VnMod::Hat, VnTone::Grave, true},
    {"Ẩ", 'a', VnMod::Hat, VnTone::Hook, true},
    {"Ẫ", 'a', VnMod::Hat, VnTone::Tilde, true},
    {"Ậ", 'a', VnMod::Hat, VnTone::Dot, true},

    // === 'e' thường ===
    {"e", 'e', VnMod::None, VnTone::None, false},
    {"é", 'e', VnMod::None, VnTone::Acute, false},
    {"è", 'e', VnMod::None, VnTone::Grave, false},
    {"ẻ", 'e', VnMod::None, VnTone::Hook, false},
    {"ẽ", 'e', VnMod::None, VnTone::Tilde, false},
    {"ẹ", 'e', VnMod::None, VnTone::Dot, false},

    {"ê", 'e', VnMod::Hat, VnTone::None, false},
    {"ế", 'e', VnMod::Hat, VnTone::Acute, false},
    {"ề", 'e', VnMod::Hat, VnTone::Grave, false},
    {"ể", 'e', VnMod::Hat, VnTone::Hook, false},
    {"ễ", 'e', VnMod::Hat, VnTone::Tilde, false},
    {"ệ", 'e', VnMod::Hat, VnTone::Dot, false},

    // === 'E' hoa ===
    {"E", 'e', VnMod::None, VnTone::None, true},
    {"É", 'e', VnMod::None, VnTone::Acute, true},
    {"È", 'e', VnMod::None, VnTone::Grave, true},
    {"Ẻ", 'e', VnMod::None, VnTone::Hook, true},
    {"Ẽ", 'e', VnMod::None, VnTone::Tilde, true},
    {"Ẹ", 'e', VnMod::None, VnTone::Dot, true},

    {"Ê", 'e', VnMod::Hat, VnTone::None, true},
    {"Ế", 'e', VnMod::Hat, VnTone::Acute, true},
    {"Ề", 'e', VnMod::Hat, VnTone::Grave, true},
    {"Ể", 'e', VnMod::Hat, VnTone::Hook, true},
    {"Ễ", 'e', VnMod::Hat, VnTone::Tilde, true},
    {"Ệ", 'e', VnMod::Hat, VnTone::Dot, true},

    // === 'i' thường ===
    {"i", 'i', VnMod::None, VnTone::None, false},
    {"í", 'i', VnMod::None, VnTone::Acute, false},
    {"ì", 'i', VnMod::None, VnTone::Grave, false},
    {"ỉ", 'i', VnMod::None, VnTone::Hook, false},
    {"ĩ", 'i', VnMod::None, VnTone::Tilde, false},
    {"ị", 'i', VnMod::None, VnTone::Dot, false},

    // === 'I' hoa ===
    {"I", 'i', VnMod::None, VnTone::None, true},
    {"Í", 'i', VnMod::None, VnTone::Acute, true},
    {"Ì", 'i', VnMod::None, VnTone::Grave, true},
    {"Ỉ", 'i', VnMod::None, VnTone::Hook, true},
    {"Ĩ", 'i', VnMod::None, VnTone::Tilde, true},
    {"Ị", 'i', VnMod::None, VnTone::Dot, true},

    // === 'o' thường ===
    {"o", 'o', VnMod::None, VnTone::None, false},
    {"ó", 'o', VnMod::None, VnTone::Acute, false},
    {"ò", 'o', VnMod::None, VnTone::Grave, false},
    {"ỏ", 'o', VnMod::None, VnTone::Hook, false},
    {"õ", 'o', VnMod::None, VnTone::Tilde, false},
    {"ọ", 'o', VnMod::None, VnTone::Dot, false},

    {"ô", 'o', VnMod::Hat, VnTone::None, false},
    {"ố", 'o', VnMod::Hat, VnTone::Acute, false},
    {"ồ", 'o', VnMod::Hat, VnTone::Grave, false},
    {"ổ", 'o', VnMod::Hat, VnTone::Hook, false},
    {"ỗ", 'o', VnMod::Hat, VnTone::Tilde, false},
    {"ộ", 'o', VnMod::Hat, VnTone::Dot, false},

    {"ơ", 'o', VnMod::HornOrBreve, VnTone::None, false},
    {"ớ", 'o', VnMod::HornOrBreve, VnTone::Acute, false},
    {"ờ", 'o', VnMod::HornOrBreve, VnTone::Grave, false},
    {"ở", 'o', VnMod::HornOrBreve, VnTone::Hook, false},
    {"ỡ", 'o', VnMod::HornOrBreve, VnTone::Tilde, false},
    {"ợ", 'o', VnMod::HornOrBreve, VnTone::Dot, false},

    // === 'O' hoa ===
    {"O", 'o', VnMod::None, VnTone::None, true},
    {"Ó", 'o', VnMod::None, VnTone::Acute, true},
    {"Ò", 'o', VnMod::None, VnTone::Grave, true},
    {"Ỏ", 'o', VnMod::None, VnTone::Hook, true},
    {"Õ", 'o', VnMod::None, VnTone::Tilde, true},
    {"Ọ", 'o', VnMod::None, VnTone::Dot, true},

    {"Ô", 'o', VnMod::Hat, VnTone::None, true},
    {"Ố", 'o', VnMod::Hat, VnTone::Acute, true},
    {"Ồ", 'o', VnMod::Hat, VnTone::Grave, true},
    {"Ổ", 'o', VnMod::Hat, VnTone::Hook, true},
    {"Ỗ", 'o', VnMod::Hat, VnTone::Tilde, true},
    {"Ộ", 'o', VnMod::Hat, VnTone::Dot, true},

    {"Ơ", 'o', VnMod::HornOrBreve, VnTone::None, true},
    {"Ớ", 'o', VnMod::HornOrBreve, VnTone::Acute, true},
    {"Ờ", 'o', VnMod::HornOrBreve, VnTone::Grave, true},
    {"Ở", 'o', VnMod::HornOrBreve, VnTone::Hook, true},
    {"Ỡ", 'o', VnMod::HornOrBreve, VnTone::Tilde, true},
    {"Ợ", 'o', VnMod::HornOrBreve, VnTone::Dot, true},

    // === 'u' thường ===
    {"u", 'u', VnMod::None, VnTone::None, false},
    {"ú", 'u', VnMod::None, VnTone::Acute, false},
    {"ù", 'u', VnMod::None, VnTone::Grave, false},
    {"ủ", 'u', VnMod::None, VnTone::Hook, false},
    {"ũ", 'u', VnMod::None, VnTone::Tilde, false},
    {"ụ", 'u', VnMod::None, VnTone::Dot, false},

    {"ư", 'u', VnMod::HornOrBreve, VnTone::None, false},
    {"ứ", 'u', VnMod::HornOrBreve, VnTone::Acute, false},
    {"ừ", 'u', VnMod::HornOrBreve, VnTone::Grave, false},
    {"ử", 'u', VnMod::HornOrBreve, VnTone::Hook, false},
    {"ữ", 'u', VnMod::HornOrBreve, VnTone::Tilde, false},
    {"ự", 'u', VnMod::HornOrBreve, VnTone::Dot, false},

    // === 'U' hoa ===
    {"U", 'u', VnMod::None, VnTone::None, true},
    {"Ú", 'u', VnMod::None, VnTone::Acute, true},
    {"Ù", 'u', VnMod::None, VnTone::Grave, true},
    {"Ủ", 'u', VnMod::None, VnTone::Hook, true},
    {"Ũ", 'u', VnMod::None, VnTone::Tilde, true},
    {"Ụ", 'u', VnMod::None, VnTone::Dot, true},

    {"Ư", 'u', VnMod::HornOrBreve, VnTone::None, true},
    {"Ứ", 'u', VnMod::HornOrBreve, VnTone::Acute, true},
    {"Ừ", 'u', VnMod::HornOrBreve, VnTone::Grave, true},
    {"Ử", 'u', VnMod::HornOrBreve, VnTone::Hook, true},
    {"Ữ", 'u', VnMod::HornOrBreve, VnTone::Tilde, true},
    {"Ự", 'u', VnMod::HornOrBreve, VnTone::Dot, true},

    // === 'y' thường ===
    {"y", 'y', VnMod::None, VnTone::None, false},
    {"ý", 'y', VnMod::None, VnTone::Acute, false},
    {"ỳ", 'y', VnMod::None, VnTone::Grave, false},
    {"ỷ", 'y', VnMod::None, VnTone::Hook, false},
    {"ỹ", 'y', VnMod::None, VnTone::Tilde, false},
    {"ỵ", 'y', VnMod::None, VnTone::Dot, false},

    // === 'Y' hoa ===
    {"Y", 'y', VnMod::None, VnTone::None, true},
    {"Ý", 'y', VnMod::None, VnTone::Acute, true},
    {"Ỳ", 'y', VnMod::None, VnTone::Grave, true},
    {"Ỷ", 'y', VnMod::None, VnTone::Hook, true},
    {"Ỹ", 'y', VnMod::None, VnTone::Tilde, true},
    {"Ỵ", 'y', VnMod::None, VnTone::Dot, true},

    // === 'd' / 'đ' ===
    {"d", 'd', VnMod::None, VnTone::None, false},
    {"đ", 'd', VnMod::Crossed, VnTone::None, false},
    {"D", 'd', VnMod::None, VnTone::None, true},
    {"Đ", 'd', VnMod::Crossed, VnTone::None, true},
};

static constexpr size_t MAPPING_COUNT = sizeof(MAPPINGS) / sizeof(MAPPINGS[0]);

const char* vnCharToUtf8(const VnChar& c, char* fallbackBuf) {
  for (size_t i = 0; i < MAPPING_COUNT; ++i) {
    if (MAPPINGS[i].base == c.base && MAPPINGS[i].mod == c.mod && MAPPINGS[i].tone == c.tone &&
        MAPPINGS[i].isUpper == c.isUpper) {
      return MAPPINGS[i].utf8;
    }
  }
  fallbackBuf[0] = c.isUpper ? static_cast<char>(toupper(static_cast<unsigned char>(c.base))) : c.base;
  fallbackBuf[1] = '\0';
  return fallbackBuf;
}

bool utf8ToVnChar(const char* utf8, size_t len, VnChar& out) {
  for (size_t i = 0; i < MAPPING_COUNT; ++i) {
    if (strncmp(utf8, MAPPINGS[i].utf8, len) == 0 && MAPPINGS[i].utf8[len] == '\0') {
      out.base = MAPPINGS[i].base;
      out.mod = MAPPINGS[i].mod;
      out.tone = MAPPINGS[i].tone;
      out.isUpper = MAPPINGS[i].isUpper;
      return true;
    }
  }
  return false;
}

bool isVowel(char base) {
  return base == 'a' || base == 'e' || base == 'i' || base == 'o' || base == 'u' || base == 'y';
}

// Xác định vị trí nguyên âm chính trong từ để đặt dấu thanh
int findTargetVowelIndex(const VnChar* chars, size_t len) {
  int vowelIdx[16];
  int vowelCount = 0;
  for (size_t i = 0; i < len && vowelCount < 16; ++i) {
    if (isVowel(chars[i].base)) {
      vowelIdx[vowelCount++] = static_cast<int>(i);
    }
  }
  if (vowelCount == 0) return -1;
  if (vowelCount == 1) return vowelIdx[0];

  bool hasFinalConsonant = (vowelIdx[vowelCount - 1] < static_cast<int>(len) - 1);

  int firstVowel = 0;
  if (len >= 2 && chars[0].base == 'q' && chars[1].base == 'u' && vowelCount > 1) {
    firstVowel = 1;  // Âm đệm trong "qu"
  } else if (len >= 2 && chars[0].base == 'g' && chars[1].base == 'i' && vowelCount > 1) {
    firstVowel = 1;  // Phụ âm ghép "gi" (già, gió, giúp)
  }

  if (vowelCount - firstVowel == 1) {
    return vowelIdx[firstVowel];
  }

  // Trường hợp 1: Có phụ âm cuối (ví dụ: toan, tieng, nguyen, hoang, muon...)
  if (hasFinalConsonant) {
    // Nếu có cụm "ươ": dấu luôn đặt trên 'ơ' (đường, mượn, nước...)
    for (int i = firstVowel; i < vowelCount - 1; ++i) {
      if (chars[vowelIdx[i]].base == 'u' && chars[vowelIdx[i]].mod == VnMod::HornOrBreve &&
          chars[vowelIdx[i + 1]].base == 'o' && chars[vowelIdx[i + 1]].mod == VnMod::HornOrBreve) {
        return vowelIdx[i + 1];
      }
    }

    // Ưu tiên nguyên âm có dấu mũ hoặc móc
    for (int i = firstVowel; i < vowelCount; ++i) {
      if (chars[vowelIdx[i]].mod != VnMod::None) {
        return vowelIdx[i];
      }
    }
    // Dấu đặt vào nguyên âm đứng ngay trước phụ âm cuối
    return vowelIdx[vowelCount - 1];
  }

  // Trường hợp 2: Không có phụ âm cuối (từ mở: hoa, thuy, thue, mia, mua, nguoi...)
  // Kiểm tra cụm "ươ" (ví dụ: cửa, lửa, hươu)
  for (int i = firstVowel; i < vowelCount - 1; ++i) {
    if (chars[vowelIdx[i]].base == 'u' && chars[vowelIdx[i]].mod == VnMod::HornOrBreve &&
        chars[vowelIdx[i + 1]].base == 'o' && chars[vowelIdx[i + 1]].mod == VnMod::HornOrBreve) {
      return vowelIdx[i + 1];  // Đặt trên 'ơ'
    }
  }

  // Nguyên âm có mũ/móc duy nhất (ví dụ "thuế" u-ê -> ê)
  for (int i = firstVowel; i < vowelCount; ++i) {
    if (chars[vowelIdx[i]].mod != VnMod::None) {
      return vowelIdx[i];
    }
  }

  // Các cặp nguyên âm mở đặc biệt:
  if (vowelCount - firstVowel == 2) {
    char v1 = chars[vowelIdx[firstVowel]].base;
    char v2 = chars[vowelIdx[firstVowel + 1]].base;
    // Cặp "oa", "oe", "uy": đặt trên nguyên âm thứ hai (hòa, hòe, thủy)
    if ((v1 == 'o' && v2 == 'a') || (v1 == 'o' && v2 == 'e') || (v1 == 'u' && v2 == 'y')) {
      return vowelIdx[firstVowel + 1];
    }
    // Cặp "ia", "ua": đặt trên nguyên âm thứ nhất (mía, múa)
    if ((v1 == 'i' && v2 == 'a') || (v1 == 'u' && v2 == 'a')) {
      return vowelIdx[firstVowel];
    }
    // Các cặp có bán âm cuối (i, y, o, u đứng sau cùng): mái, sáo, mèo, ngồi, túi
    if (v2 == 'i' || v2 == 'y' || v2 == 'o' || v2 == 'u') {
      return vowelIdx[firstVowel];
    }
  }

  return vowelIdx[firstVowel + 1];
}

}  // namespace

bool processKey(std::string& text, size_t& cursorPos, char key) {
  if (cursorPos > text.length()) cursorPos = text.length();

  // 1. Quét ngược tìm điểm bắt đầu của từ hiện tại
  size_t wordStart = cursorPos;
  while (wordStart > 0) {
    uint8_t b = static_cast<uint8_t>(text[wordStart - 1]);
    if (b < 0x80) {
      char c = static_cast<char>(b);
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
        break;  // Ký tự phân cách (space, dấu câu, số...)
      }
      wordStart--;
    } else {
      // Byte UTF-8: tìm đầu ký tự UTF-8 (byte không có dạng 10xxxxxx)
      size_t prev = wordStart - 1;
      while (prev > 0 && (static_cast<uint8_t>(text[prev]) & 0xC0) == 0x80) {
        prev--;
      }
      wordStart = prev;
    }
  }

  const std::string wordStr = text.substr(wordStart, cursorPos - wordStart);

  // 2. Phân rã wordStr thành danh sách VnChar
  VnChar chars[32];
  size_t charCount = 0;
  size_t byteIdx = 0;
  while (byteIdx < wordStr.length() && charCount < 32) {
    uint8_t b = static_cast<uint8_t>(wordStr[byteIdx]);
    if (b < 0x80) {
      char c = static_cast<char>(b);
      chars[charCount++] = VnChar{static_cast<char>(tolower(static_cast<unsigned char>(c))), VnMod::None,
                                  VnTone::None, static_cast<bool>(isupper(static_cast<unsigned char>(c)))};
      byteIdx++;
    } else {
      size_t charBytes = 1;
      if ((b & 0xE0) == 0xC0)
        charBytes = 2;
      else if ((b & 0xF0) == 0xE0)
        charBytes = 3;
      else if ((b & 0xF8) == 0xF0)
        charBytes = 4;

      if (byteIdx + charBytes <= wordStr.length()) {
        VnChar vn;
        if (utf8ToVnChar(wordStr.data() + byteIdx, charBytes, vn)) {
          chars[charCount++] = vn;
        } else {
          // Ký tự không nhận biết được, không thuộc từ tiếng Việt hợp lệ
          return false;
        }
      }
      byteIdx += charBytes;
    }
  }

  char k = static_cast<char>(tolower(static_cast<unsigned char>(key)));
  bool isUpper = static_cast<bool>(isupper(static_cast<unsigned char>(key)));

  bool transformed = false;

  // 3. Xử lý các quy tắc gõ Telex
  // --- Phím 'w': móc / ư / ă / ơ / ươ ---
  if (k == 'w') {
    if (charCount == 0) {
      // 'w' đứng một mình -> 'ư'
      chars[charCount++] = VnChar{'u', VnMod::HornOrBreve, VnTone::None, isUpper};
      transformed = true;
    } else {
      // Kiểm tra cụm "uo" (ví dụ: duong, muon, tuoi)
      int uoIdx = -1;
      for (int i = 0; i < static_cast<int>(charCount) - 1; ++i) {
        if (chars[i].base == 'u' && chars[i + 1].base == 'o') {
          uoIdx = i;
          break;
        }
      }
      if (uoIdx >= 0) {
        if (chars[uoIdx].mod == VnMod::None && chars[uoIdx + 1].mod == VnMod::None) {
          chars[uoIdx].mod = VnMod::HornOrBreve;
          chars[uoIdx + 1].mod = VnMod::HornOrBreve;
          transformed = true;
        } else if (chars[uoIdx].mod == VnMod::HornOrBreve && chars[uoIdx + 1].mod == VnMod::HornOrBreve) {
          // Undo: trả về uo + w
          chars[uoIdx].mod = VnMod::None;
          chars[uoIdx + 1].mod = VnMod::None;
          chars[charCount++] = VnChar{'w', VnMod::None, VnTone::None, isUpper};
          transformed = true;
        }
      }

      if (!transformed) {
        // Tìm nguyên âm 'u', 'o', 'a' gần nhất từ cuối lên
        for (int i = static_cast<int>(charCount) - 1; i >= 0; --i) {
          if (chars[i].base == 'a') {
            if (chars[i].mod == VnMod::None) {
              chars[i].mod = VnMod::HornOrBreve;  // 'ă'
              transformed = true;
            } else if (chars[i].mod == VnMod::HornOrBreve) {
              // Undo
              chars[i].mod = VnMod::None;
              chars[charCount++] = VnChar{'w', VnMod::None, VnTone::None, isUpper};
              transformed = true;
            }
            break;
          } else if (chars[i].base == 'o') {
            if (chars[i].mod == VnMod::None) {
              chars[i].mod = VnMod::HornOrBreve;  // 'ơ'
              transformed = true;
            } else if (chars[i].mod == VnMod::HornOrBreve) {
              chars[i].mod = VnMod::None;
              chars[charCount++] = VnChar{'w', VnMod::None, VnTone::None, isUpper};
              transformed = true;
            }
            break;
          } else if (chars[i].base == 'u') {
            if (chars[i].mod == VnMod::None) {
              chars[i].mod = VnMod::HornOrBreve;  // 'ư'
              transformed = true;
            } else if (chars[i].mod == VnMod::HornOrBreve) {
              chars[i].mod = VnMod::None;
              chars[charCount++] = VnChar{'w', VnMod::None, VnTone::None, isUpper};
              transformed = true;
            }
            break;
          }
        }
      }

      if (!transformed) {
        // Sau phụ âm đứng một mình (thw -> thư, tw -> tư)
        chars[charCount++] = VnChar{'u', VnMod::HornOrBreve, VnTone::None, isUpper};
        transformed = true;
      }
    }
  }
  // --- Phím 'a': 'aa' -> 'â' ---
  else if (k == 'a' && charCount > 0) {
    if (chars[charCount - 1].base == 'a') {
      if (chars[charCount - 1].mod == VnMod::None) {
        chars[charCount - 1].mod = VnMod::Hat;  // 'â'
        transformed = true;
      } else if (chars[charCount - 1].mod == VnMod::Hat) {
        // Undo 'â' + 'a' -> "aa"
        chars[charCount - 1].mod = VnMod::None;
        chars[charCount++] = VnChar{'a', VnMod::None, VnTone::None, isUpper};
        transformed = true;
      }
    }
  }
  // --- Phím 'e': 'ee' -> 'ê' ---
  else if (k == 'e' && charCount > 0) {
    if (chars[charCount - 1].base == 'e') {
      if (chars[charCount - 1].mod == VnMod::None) {
        chars[charCount - 1].mod = VnMod::Hat;  // 'ê'
        transformed = true;
      } else if (chars[charCount - 1].mod == VnMod::Hat) {
        chars[charCount - 1].mod = VnMod::None;
        chars[charCount++] = VnChar{'e', VnMod::None, VnTone::None, isUpper};
        transformed = true;
      }
    }
  }
  // --- Phím 'o': 'oo' -> 'ô' ---
  else if (k == 'o' && charCount > 0) {
    if (chars[charCount - 1].base == 'o') {
      if (chars[charCount - 1].mod == VnMod::None) {
        chars[charCount - 1].mod = VnMod::Hat;  // 'ô'
        transformed = true;
      } else if (chars[charCount - 1].mod == VnMod::Hat) {
        chars[charCount - 1].mod = VnMod::None;
        chars[charCount++] = VnChar{'o', VnMod::None, VnTone::None, isUpper};
        transformed = true;
      }
    }
  }
  // --- Phím 'd': 'dd' -> 'đ' ---
  else if (k == 'd' && charCount > 0) {
    if (chars[charCount - 1].base == 'd') {
      if (chars[charCount - 1].mod == VnMod::None) {
        chars[charCount - 1].mod = VnMod::Crossed;  // 'đ'
        // Nếu ký tự trước viết hoa thì giữ hoa
        transformed = true;
      } else if (chars[charCount - 1].mod == VnMod::Crossed) {
        chars[charCount - 1].mod = VnMod::None;
        chars[charCount++] = VnChar{'d', VnMod::None, VnTone::None, isUpper};
        transformed = true;
      }
    }
  }
  // --- Các phím Dấu Thanh: s (sắc), f (huyền), r (hỏi), x (ngã), j (nặng), z (xóa dấu) ---
  else if (k == 's' || k == 'f' || k == 'r' || k == 'x' || k == 'j' || k == 'z') {
    VnTone newTone = VnTone::None;
    if (k == 's')
      newTone = VnTone::Acute;
    else if (k == 'f')
      newTone = VnTone::Grave;
    else if (k == 'r')
      newTone = VnTone::Hook;
    else if (k == 'x')
      newTone = VnTone::Tilde;
    else if (k == 'j')
      newTone = VnTone::Dot;

    int targetIdx = findTargetVowelIndex(chars, charCount);
    if (targetIdx >= 0) {
      VnTone curTone = chars[targetIdx].tone;
      if (k == 'z') {
        if (curTone != VnTone::None) {
          chars[targetIdx].tone = VnTone::None;
          transformed = true;
        }
      } else if (curTone == newTone) {
        // Undo: gõ lặp lại phím dấu -> hủy dấu và chèn phím dấu đó
        chars[targetIdx].tone = VnTone::None;
        chars[charCount++] = VnChar{k, VnMod::None, VnTone::None, isUpper};
        transformed = true;
      } else {
        // Đặt dấu thanh mới
        for (size_t i = 0; i < charCount; ++i) {
          chars[i].tone = VnTone::None;  // Xóa dấu thanh ở mọi vị trí khác
        }
        chars[targetIdx].tone = newTone;
        transformed = true;
      }
    }
  }

  if (!transformed) {
    return false;
  }

  // 4. Chuyển mảng VnChar thành chuỗi UTF-8 kết quả
  std::string newWord;
  newWord.reserve(charCount * 3);
  char fallback[2];
  for (size_t i = 0; i < charCount; ++i) {
    newWord.append(vnCharToUtf8(chars[i], fallback));
  }

  // 5. Cập nhật text và cursorPos
  text.replace(wordStart, cursorPos - wordStart, newWord);
  cursorPos = wordStart + newWord.length();
  return true;
}

}  // namespace vietnamese_telex
