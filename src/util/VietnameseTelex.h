#pragma once

#include <cstddef>
#include <string>

namespace vietnamese_telex {

enum class VnMod : uint8_t {
  None = 0,
  Hat = 1,          // â, ê, ô
  HornOrBreve = 2,  // ă, ơ, ư
  Crossed = 3       // đ
};

enum class VnTone : uint8_t {
  None = 0,
  Acute = 1,  // Sắc (s)
  Grave = 2,  // Huyền (f)
  Hook = 3,   // Hỏi (r)
  Tilde = 4,  // Ngã (x)
  Dot = 5     // Nặng (j)
};

struct VnChar {
  char base = 0;
  VnMod mod = VnMod::None;
  VnTone tone = VnTone::None;
  bool isUpper = false;
};

// Xử lý một ký tự mới được gõ theo phương pháp Telex tiếng Việt.
// text: Toàn bộ chuỗi văn bản đang nhập
// cursorPos: Vị trí con trỏ (offset byte) trong chuỗi text
// key: Ký tự ASCII vừa gõ ('a'..'z', 'A'..'Z')
//
// Trả về true nếu có biến đổi Telex (text & cursorPos được cập nhật).
// Trả về false nếu không có biến đổi nào (caller chèn ký tự bình thường).
bool processKey(std::string& text, size_t& cursorPos, char key);

}  // namespace vietnamese_telex
