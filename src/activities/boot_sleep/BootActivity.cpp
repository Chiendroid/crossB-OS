#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;
  const int cy = pageHeight / 2 - 35;

  renderer.clearScreen();

  // 1. Biểu tượng Logo crossB OS: Khung Squircle bo tròn hiện đại (100x100)
  constexpr int boxSize = 100;
  const int boxX = cx - boxSize / 2;
  const int boxY = cy - boxSize / 2;
  renderer.drawRoundedRect(boxX, boxY, boxSize, boxSize, 3, 22, true);

  // Đường gáy sách ở giữa
  renderer.fillRect(cx - 1, cy - 26, 3, 52, true);

  // Cánh sách bên trái: Trang sách mở với đường kẻ đọc
  renderer.drawRoundedRect(cx - 36, cy - 22, 30, 44, 2, 6, true);
  renderer.drawLine(cx - 30, cy - 10, cx - 12, cy - 10, 2, true);
  renderer.drawLine(cx - 30, cy + 4, cx - 12, cy + 4, 2, true);

  // Cánh sách bên phải: Tạo hình 2 cung tròn của chữ B (crossB)
  renderer.drawRoundedRect(cx + 6, cy - 22, 28, 23, 2, 8, true);
  renderer.drawRoundedRect(cx + 6, cy - 1, 30, 23, 2, 8, true);

  // 2. Tên hệ điều hành crossB OS
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, cy + 80, "crossB OS", true, EpdFontFamily::BOLD);

  // 3. Trạng thái khởi động
  renderer.drawCenteredText(SMALL_FONT_ID, cy + 110, tr(STR_BOOTING));

  // 4. Phiên bản hệ điều hành
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);

  renderer.displayBuffer();
}
