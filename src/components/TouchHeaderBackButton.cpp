#include "TouchHeaderBackButton.h"

#include <FreeInkUIIcon.h>

#include <algorithm>

#include "UIThemeTokens.h"
#include "components/CompactHeader.h"
#include "components/HeaderDate.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/touchHeaderIcons.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace TouchHeaderBackButton {

namespace {
constexpr int actionSize = 52;
constexpr int touchSize = 68;
constexpr int titleGap = 8;
constexpr int defaultRightReserve = 52;

int effectiveVerticalOffset(const Layout& layout, const Rect& header, const int requestedOffset) {
  const int iconBottom = layout.iconRect.y + (layout.iconRect.height + ICON_SIZE) / 2;
  const int availableOffset = std::max(0, header.y + header.height - iconBottom);
  return std::clamp(requestedOffset, 0, availableOffset);
}
}  // namespace

Layout layout(const Rect& header) {
  const int actionWidth = std::min(actionSize, header.width);
  const int actionHeight = std::min(actionSize, header.height);
  const int actionX = header.x;
  const int actionY = header.y + (header.height - actionHeight) / 2;
  const int touchWidth = std::min(touchSize, header.width);
  const int touchHeight = touchSize;
  const int touchX = std::max(header.x, actionX + (actionWidth - touchWidth) / 2);
  const int touchY = std::max(0, actionY + (actionHeight - touchHeight) / 2);
  return {Rect{actionX, actionY, actionWidth, actionHeight}, Rect{touchX, touchY, touchWidth, touchHeight},
          actionX + actionWidth + titleGap};
}

Rect standardHeaderRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight};
}

int height(const ThemeMetrics& metrics, const MappedInputManager& input) {
  return input.hasTouchHardware() ? CompactHeader::height(metrics) : metrics.headerHeight;
}

Rect headerRect(const GfxRenderer& renderer, const MappedInputManager& input) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{0, metrics.topPadding, renderer.getScreenWidth(), height(metrics, input)};
}

Rect compactHeaderRect(const GfxRenderer& renderer) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return Rect{0, metrics.topPadding, renderer.getScreenWidth(),
              CompactHeader::headerBottomY(metrics) - metrics.topPadding};
}

int contentTop(const GfxRenderer& renderer, const MappedInputManager& input, const int areaTop) {
  const auto header = headerRect(renderer, input);
  return areaTop + header.y + header.height;
}

bool wasTapped(const MappedInputManager& input, const Rect& header) {
  if (!input.hasTouchHardware()) return false;
  const Rect touchRect = layout(header).touchRect;
  return input.wasTapInRect(touchRect.x, touchRect.y, touchRect.width, touchRect.height);
}

bool wasTapped(const MappedInputManager& input, const GfxRenderer& renderer) {
  return wasTapped(input, headerRect(renderer, input));
}

void draw(GfxRenderer& renderer, const Rect& header, const char* title, const bool readerContext,
          const int rightReserve, const char* subtitle, const int verticalOffset, const bool showStatus) {
  auto target = makeUiTarget(renderer);
  draw(renderer, target, header, title, readerContext, rightReserve, subtitle, verticalOffset, showStatus);
}

void draw(GfxRenderer& renderer, fui::GfxRendererTarget& target, const Rect& header, const char* title,
          const bool readerContext, const int rightReserve, const char* subtitle, const int verticalOffset,
          const bool showStatus) {
  Layout back = layout(header);
  const int offset = effectiveVerticalOffset(back, header, verticalOffset);
  back.iconRect.y += offset;
  GUI.drawHeader(renderer, header, "", subtitle, false);

  fui::TextStyle titleStyle = uiThemeTokens(target).titleText;
  titleStyle.align = fui::TextAlign::Left;
  titleStyle.maxLines = 1;
  const int reservedRight = rightReserve > 0 ? rightReserve : defaultRightReserve;
  const int titleWidth = std::max(0, header.x + header.width - reservedRight - back.titleX);
  target.text(fui::Rect{static_cast<int16_t>(back.titleX), static_cast<int16_t>(back.iconRect.y),
                        static_cast<int16_t>(titleWidth), static_cast<int16_t>(back.iconRect.height)},
              title, titleStyle);

  const int iconX = back.iconRect.x + (back.iconRect.width - ICON_SIZE) / 2;
  const int iconY = back.iconRect.y + (back.iconRect.height - ICON_SIZE) / 2;
  target.bitmap(fui::Rect{static_cast<int16_t>(iconX), static_cast<int16_t>(iconY), ICON_SIZE, ICON_SIZE},
                fui::bitmapFromIcon(icon_back_32), fui::BitmapMode::Center);
}

void drawCompact(GfxRenderer& renderer, const char* title, const bool readerContext, const bool showDate,
                 const int verticalOffset) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect header = compactHeaderRect(renderer);
  const int rightReserve =
      metrics.batteryWidth + 2 * metrics.headerSidePadding + (showDate ? headerDateReservedWidth(renderer) : 0);
  draw(renderer, header, title, readerContext, rightReserve, nullptr, verticalOffset);
  if (showDate) {
    const Layout back = layout(header);
    const int offset = effectiveVerticalOffset(back, header, verticalOffset);
    const int titleBaselineY = back.iconRect.y + offset +
                               std::max(0, (back.iconRect.height - renderer.getLineHeight(UI_12_FONT_ID)) / 2) +
                               renderer.getFontAscenderSize(UI_12_FONT_ID);
    drawHeaderDateAtBaseline(renderer, renderer.getScreenWidth(), titleBaselineY);
  }
}

void drawSquircleBackButton(GfxRenderer& renderer, const int x, const int y, const int size, const int radius) {
  renderer.fillRoundedRect(x, y, size, size, radius, Color::White);
  renderer.drawRoundedRect(x, y, size, size, 2, radius, true);

  const int cx = x + size / 2;
  const int cy = y + size / 2;
  const int halfH = std::max(6, size / 5);
  const int halfW = std::max(4, size / 9);

  // Chevron trái nét đậm 2px
  renderer.drawLine(cx + halfW - 1, cy - halfH, cx - halfW - 1, cy, 2, true);
  renderer.drawLine(cx - halfW - 1, cy, cx + halfW - 1, cy + halfH, 2, true);
}

void drawSquircleMenuButton(GfxRenderer& renderer, const int x, const int y, const int size, const int radius) {
  renderer.fillRoundedRect(x, y, size, size, radius, Color::White);
  renderer.drawRoundedRect(x, y, size, size, 2, radius, true);

  const int cx = x + size / 2;
  const int cy = y + size / 2;
  const int barHalfW = std::max(7, size / 4);
  const int gap = std::max(5, size / 7);

  renderer.drawLine(cx - barHalfW, cy - gap, cx + barHalfW, cy - gap, 2, true);
  renderer.drawLine(cx - barHalfW, cy,       cx + barHalfW, cy,       2, true);
  renderer.drawLine(cx - barHalfW, cy + gap, cx + barHalfW, cy + gap, 2, true);
}

void drawSquircleChevronButton(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                               const bool isRight, const int radius) {
  renderer.fillRoundedRect(x, y, w, h, radius, Color::White);
  renderer.drawRoundedRect(x, y, w, h, 1, radius, true);

  const int cx = x + w / 2;
  const int cy = y + h / 2;
  const int halfH = std::max(5, h / 4);
  const int halfW = std::max(3, w / 8);

  if (isRight) {
    renderer.drawLine(cx - halfW, cy - halfH, cx + halfW, cy, 2, true);
    renderer.drawLine(cx + halfW, cy, cx - halfW, cy + halfH, 2, true);
  } else {
    renderer.drawLine(cx + halfW, cy - halfH, cx - halfW, cy, 2, true);
    renderer.drawLine(cx - halfW, cy, cx + halfW, cy + halfH, 2, true);
  }
}

}  // namespace TouchHeaderBackButton
