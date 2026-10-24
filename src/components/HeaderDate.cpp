#include "HeaderDate.h"

#include <GfxRenderer.h>
#include <HalClock.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "components/themes/BaseTheme.h"
#include "fontIds.h"

namespace {
constexpr int kHeaderDateRightInset = 12;
constexpr int kHeaderDateBottomGap = 10;

bool formatHeaderDateImpl(char* buf, const size_t len) {
  if (!halClock.isAvailable()) return false;
  struct tm t{};
  if (!halClock.localTime(t)) return false;
  snprintf(buf, len, "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  return true;
}
}  // namespace

bool formatHeaderDateText(char* buffer, const size_t length) { return formatHeaderDateImpl(buffer, length); }

int headerDateReservedWidth(const GfxRenderer& renderer) {
  char dateBuf[32];
  if (!formatHeaderDateImpl(dateBuf, sizeof(dateBuf))) return 0;

  return renderer.getTextWidth(UI_10_FONT_ID, dateBuf) + kHeaderDateRightInset;
}

int headerDateLineBottomY(const GfxRenderer&, const ThemeMetrics& metrics, const int headerHeight) {
  const int effectiveHeaderHeight = headerHeight >= 0 ? headerHeight : metrics.headerHeight;
  return metrics.topPadding + effectiveHeaderHeight - kHeaderDateBottomGap;
}

void drawHeaderDate(const GfxRenderer& renderer, const int pageWidth, const ThemeMetrics& metrics,
                    const int headerHeight) {
  drawHeaderDateAtLineBottom(renderer, pageWidth, headerDateLineBottomY(renderer, metrics, headerHeight));
}

void drawHeaderDateAtLineBottom(const GfxRenderer& renderer, const int pageWidth, const int lineBottomY) {
  constexpr int dateFontId = UI_10_FONT_ID;
  drawHeaderDateAtBaseline(renderer, pageWidth,
                           lineBottomY - renderer.getLineHeight(dateFontId) + renderer.getFontAscenderSize(dateFontId));
}

void drawHeaderDateAtBaseline(const GfxRenderer& renderer, const int pageWidth, const int baselineY) {
  char dateBuf[32];
  if (!formatHeaderDateImpl(dateBuf, sizeof(dateBuf))) return;

  constexpr int dateFontId = UI_10_FONT_ID;
  const int textWidth = renderer.getTextWidth(dateFontId, dateBuf);
  const int dateX = pageWidth - kHeaderDateRightInset - textWidth;
  const int dateY = baselineY - renderer.getFontAscenderSize(dateFontId);
  renderer.drawText(dateFontId, std::max(0, dateX), std::max(0, dateY), dateBuf);
}
