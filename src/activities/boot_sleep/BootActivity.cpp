#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const int cx = pageWidth / 2;
  const int cy = pageHeight / 2 - 30;

  renderer.clearScreen();

  // 1. Logo bitmap Logo120 sắc nét chính giữa
  constexpr int logoSize = 120;
  renderer.drawImage(Logo120, (pageWidth - logoSize) / 2, cy - logoSize / 2, logoSize, logoSize);

  // 2. Tên hệ điều hành crossB OS
  renderer.drawCenteredText(NOTOSANS_16_FONT_ID, cy + logoSize / 2 + 35, "crossB OS", true, EpdFontFamily::BOLD);

  // 3. Trạng thái khởi động
  renderer.drawCenteredText(SMALL_FONT_ID, cy + logoSize / 2 + 65, tr(STR_BOOTING));

  // 4. Phiên bản hệ điều hành ở cuối màn hình
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);

  renderer.displayBuffer();
}
