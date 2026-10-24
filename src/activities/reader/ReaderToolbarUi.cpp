#include "ReaderToolbarUi.h"

#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/icons/readerToolbarIcons.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_DISMISS = 1;  // tap anywhere on the page above the sheet
constexpr fui::ActionId ACTION_TAB = 2;      // Tab index: 0=Chữ, 1=Tiến độ, 2=Mục lục, 3=Tiện ích
constexpr fui::ActionId ACTION_SETTING = 3;  // 0=FontDec, 1=FontInc, 10..12=Spacing, 20..21=Align, 30=FontPicker
constexpr fui::ActionId ACTION_NAV = 4;      // 0=PrevChap, 1=NextChap, 2=TocPrev, 3=TocNext, 4=GoHome, 5=BookStats, 6=NightMode, 7=Frontlight
constexpr fui::ActionId ACTION_SCRUB = 5;    // dragPermille along the book
constexpr fui::ActionId ACTION_ROW = 6;      // panel list row or TOC row, value = row index

// Aliases for legacy buildToolbar/buildPanel
constexpr fui::ActionId ACTION_TOOL = ACTION_TAB;
constexpr fui::ActionId ACTION_PREV = ACTION_NAV;
constexpr fui::ActionId ACTION_NEXT = ACTION_NAV;

// Scrub row: two small round-cornered chapter buttons flanking a thin progress
// track with a round knob -- the reading page's chrome is light, so the
// controls stay slim rather than control-center sized.
constexpr int16_t kScrubButton = 36;  // chapter step buttons (square)
constexpr int16_t kScrubKnob = 16;    // round knob on the 2px progress track
constexpr int16_t kScrubGap = 12;     // air between the buttons and the track
// Tool row: a 24px glyph centred in each slot, the active slot in an outline
// pill. The whole slot is the tap target; the row height sets its size.
constexpr int16_t kToolRowH = 80;
constexpr int16_t kToolPillInset = 10;
constexpr int kToolCount = 3;
// Bottom sheet height for the panels. ListNav fits whole rows in the remaining
// list area; any spare pixels stay between the list and the switcher.
constexpr int kPanelHeightPercent = 62;
// Cap the sheet may grow to when rounding the list area up to a whole row.
constexpr int kPanelHeightMaxPercent = 72;
// Landscape has less vertical room; leave a narrow page strip for tap-to-dismiss.
constexpr int kLandscapePanelHeightPercent = 88;
}  // namespace

ReaderToolbarUi::ReaderToolbarUi(GfxRenderer& renderer) : UiAppHost(renderer) {}

void ReaderToolbarUi::begin() {
  resetUi();
  pending_ = Routed{};
  nav_.reset();
  for (fui::ActionId id = ACTION_DISMISS; id <= ACTION_ROW; ++id) {
    app.on(id, &ReaderToolbarUi::onAction, this);
  }
  app.setScreen(&ReaderToolbarUi::screenFn, this);
}

void ReaderToolbarUi::render() {
  renderUi();
  // Wrapped rows can fit fewer than the fixed-height estimate; the nav then
  // advances the viewport after layout and asks for a rebuild. Converges (top
  // only moves forward toward the selection); the bound is a backstop.
  for (int pass = 0; pass < 3 && nav_.consumeRebuildNeeded(); ++pass) renderUi();
}

ReaderToolbarUi::Routed ReaderToolbarUi::route(const MappedInputManager& input) {
  pending_ = Routed{};
  // routeHeld: the scrub track is a drag target, so held frames must reach it.
  const auto touch = routeTouch(input, false, /*routeHeld=*/true);
  pending_.routed = touch.routed;
  pending_.x = touch.snap.touchX;
  pending_.y = touch.snap.touchY;
  // Only the release commits a scrub: every held frame dispatches too
  // (dragPermille set), and re-paginating a chapter per frame would be seconds
  // of work per swipe.
  if (pending_.event == Event::Scrub && !touch.snap.touchReleased) pending_.event = Event::None;
  return pending_;
}

void ReaderToolbarUi::onAction(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<ReaderToolbarUi*>(user);
  Routed& out = self->pending_;
  out.value = event.value;
  out.permille = event.dragPermille;

  if (event.action == ACTION_DISMISS) {
    out.event = Event::Dismiss;
  } else if (event.action == ACTION_TAB) {
    out.event = Event::Tool;
    out.value = event.value;
  } else if (event.action == ACTION_SETTING) {
    if (event.value == 0) {
      out.event = Event::FontSizeDec;
    } else if (event.value == 1) {
      out.event = Event::FontSizeInc;
    } else if (event.value >= 10 && event.value <= 12) {
      out.event = Event::Spacing;
      out.value = event.value - 10;
    } else if (event.value >= 20 && event.value <= 21) {
      out.event = Event::Align;
      out.value = event.value - 20;
    } else if (event.value == 30) {
      out.event = Event::FontPicker;
    }
  } else if (event.action == ACTION_NAV) {
    if (event.value == 0) {
      out.event = Event::PrevChapter;
    } else if (event.value == 1) {
      out.event = Event::NextChapter;
    } else if (event.value == 2) {
      out.event = Event::TocPagePrev;
    } else if (event.value == 3) {
      out.event = Event::TocPageNext;
    } else if (event.value == 4) {
      out.event = Event::GoHome;
    } else if (event.value == 5) {
      out.event = Event::BookStats;
    } else if (event.value == 6) {
      out.event = Event::NightMode;
    } else if (event.value == 7) {
      out.event = Event::FrontlightControl;
    }
  } else if (event.action == ACTION_SCRUB) {
    out.event = Event::Scrub;
    if (event.dragPermille < 0) out.event = Event::None;
  } else if (event.action == ACTION_ROW) {
    out.event = Event::Row;
  }

  // A handled action repaints through the reader's own fast path, not through
  // app.invalidate(): the page underneath is the reader's to draw.
  self->app.clearTapFlash();
}

void ReaderToolbarUi::screenFn(UiScreen& screen, void* user) {
  auto* self = static_cast<ReaderToolbarUi*>(user);
  if (self->model_.panel) {
    self->buildPanel(screen);
  } else {
    self->buildDrawer(screen);
  }
}

void ReaderToolbarUi::buildDrawer(UiScreen& screen) {
  const fui::Rect safe = screen.frame().safeRect();

  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;
  sheetProps.grabberMargin = 6;
  sheetProps.grabberHeight = 4;
  sheetProps.grabberInset = 8;

  // Drawer cao 350px (trong màn hình 800px, phần trên 450px vẫn thấy trang sách)
  constexpr int16_t kDrawerHeight = 350;
  screen.sheet(sheetProps, kDrawerHeight);

  const int16_t insetX = 18;
  const int16_t contentW = static_cast<int16_t>(safe.width - 2 * insetX);
  const int16_t drawerTopY = static_cast<int16_t>(safe.height - kDrawerHeight);

  // 1. Tab Bar (4 tabs: Aa Chữ, Tiến độ, Mục lục, Tiện ích)
  const fui::Rect barRect{insetX, static_cast<int16_t>(drawerTopY + 24), contentW, 38};
  buildDrawerTabBar(screen, barRect);

  // 2. Nội dung Tab
  const fui::Rect contentRect{insetX, static_cast<int16_t>(barRect.bottom() + 12), contentW,
                             static_cast<int16_t>(safe.height - barRect.bottom() - 12)};
  switch (model_.activeTab) {
    case 0:
      buildDrawerTab0(screen, contentRect);
      break;
    case 1:
      buildDrawerTab1(screen, contentRect);
      break;
    case 2:
      buildDrawerTab2(screen, contentRect);
      break;
    case 3:
      buildDrawerTab3(screen, contentRect);
      break;
    default:
      buildDrawerTab0(screen, contentRect);
      break;
  }
}

void ReaderToolbarUi::buildDrawerTabBar(UiScreen& screen, const fui::Rect& barRect) {
  const auto& tokens = screen.theme();
  static const char* const kTabs[4] = {"Aa Ch\u1eef", "Ti\u1ebfn \u0111\u1ed9", "M\u1ee5c l\u1ee5c", "Ti\u1ec7n \u00edch"};
  constexpr int kCount = 4;
  constexpr int16_t gap = 6;
  const int16_t tabW = static_cast<int16_t>((barRect.width - (kCount - 1) * gap) / kCount);

  for (int i = 0; i < kCount; ++i) {
    const fui::Rect r{static_cast<int16_t>(barRect.x + i * (tabW + gap)), barRect.y, tabW, barRect.height};
    const bool active = (model_.activeTab == i);

    fui::TextStyle style = tokens.smallText;
    style.bold = true;
    style.align = fui::TextAlign::Center;

    if (active) {
      screen.target().fill(r, fui::Paint::solid(fui::Color::Black), 8);
      style.color = fui::Color::White;
      screen.target().text(r, kTabs[i], style);
    } else {
      screen.target().stroke(r, fui::Paint::solid(fui::Color::Black), 1, 8);
      style.color = fui::Color::Black;
      screen.target().text(r, kTabs[i], style);
    }
    screen.frame().hit(r, ACTION_TAB, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void ReaderToolbarUi::buildDrawerTab0(UiScreen& screen, const fui::Rect& contentRect) {
  const auto& tokens = screen.theme();

  fui::TextStyle labelStyle = tokens.bodyText;
  labelStyle.bold = true;
  labelStyle.color = fui::Color::Black;

  fui::TextStyle centerBold = tokens.bodyText;
  centerBold.bold = true;
  centerBold.align = fui::TextAlign::Center;

  fui::TextStyle smallCenterBold = tokens.smallText;
  smallCenterBold.bold = true;
  smallCenterBold.align = fui::TextAlign::Center;

  fui::TextStyle smallCenterWhite = smallCenterBold;
  smallCenterWhite.color = fui::Color::White;

  fui::TextStyle smallCenterBlack = smallCenterBold;
  smallCenterBlack.color = fui::Color::Black;

  int16_t curY = contentRect.y;

  // Hàng 1: Cỡ chữ (Font size [-] 14 pt [+])
  {
    screen.target().text(fui::Rect{contentRect.x, static_cast<int16_t>(curY + 6), 120, 30}, "C\u1ee1 ch\u1eef", labelStyle);

    // Nút [-]
    const fui::Rect minusRect{static_cast<int16_t>(contentRect.right() - 176), curY, 48, 38};
    screen.target().stroke(minusRect, fui::Paint::solid(fui::Color::Black), 1, 6);
    screen.target().text(minusRect, "-", centerBold);
    screen.frame().hit(minusRect, ACTION_SETTING, 0, fui::InputTouch);

    // Nhãn cỡ chữ hiện tại: ví dụ "14 pt"
    char buf[16];
    snprintf(buf, sizeof(buf), "%d pt", model_.fontSize);
    screen.target().text(fui::Rect{static_cast<int16_t>(contentRect.right() - 124), static_cast<int16_t>(curY + 6), 68, 30},
                        buf, centerBold);

    // Nút [+]
    const fui::Rect plusRect{static_cast<int16_t>(contentRect.right() - 52), curY, 48, 38};
    screen.target().stroke(plusRect, fui::Paint::solid(fui::Color::Black), 1, 6);
    screen.target().text(plusRect, "+", centerBold);
    screen.frame().hit(plusRect, ACTION_SETTING, 1, fui::InputTouch);
  }
  curY += 48;

  // Hàng 2: Giãn dòng (Line Spacing: Gọn, Vừa, Rộng)
  {
    screen.target().text(fui::Rect{contentRect.x, static_cast<int16_t>(curY + 6), 120, 30}, "Gi\u00e3n d\u00f2ng", labelStyle);

    static const char* const kSpacingNames[3] = {"G\u1ecdn", "V\u1eeba", "R\u1ed9ng"};
    constexpr int16_t btnW = 54;
    constexpr int16_t gap = 6;
    const int16_t startX = static_cast<int16_t>(contentRect.right() - (3 * btnW + 2 * gap));

    for (int k = 0; k < 3; ++k) {
      const fui::Rect btn{static_cast<int16_t>(startX + k * (btnW + gap)), curY, btnW, 36};
      const bool active = (model_.lineSpacing == k);
      if (active) {
        screen.target().fill(btn, fui::Paint::solid(fui::Color::Black), 6);
        screen.target().text(btn, kSpacingNames[k], smallCenterWhite);
      } else {
        screen.target().stroke(btn, fui::Paint::solid(fui::Color::Black), 1, 6);
        screen.target().text(btn, kSpacingNames[k], smallCenterBlack);
      }
      screen.frame().hit(btn, ACTION_SETTING, static_cast<int16_t>(10 + k), fui::InputTouch);
    }
  }
  curY += 46;

  // Hàng 3: Căn lề (Alignment: Căn đều, Căn trái)
  {
    screen.target().text(fui::Rect{contentRect.x, static_cast<int16_t>(curY + 6), 120, 30}, "C\u0103n l\u1ec1", labelStyle);

    static const char* const kAlignNames[2] = {"C\u0103n \u0111\u1ec1u", "C\u0103n tr\u00e1i"};
    constexpr int16_t btnW = 84;
    constexpr int16_t gap = 8;
    const int16_t startX = static_cast<int16_t>(contentRect.right() - (2 * btnW + gap));

    for (int k = 0; k < 2; ++k) {
      const fui::Rect btn{static_cast<int16_t>(startX + k * (btnW + gap)), curY, btnW, 36};
      const bool active = (model_.alignment == k);
      if (active) {
        screen.target().fill(btn, fui::Paint::solid(fui::Color::Black), 6);
        screen.target().text(btn, kAlignNames[k], smallCenterWhite);
      } else {
        screen.target().stroke(btn, fui::Paint::solid(fui::Color::Black), 1, 6);
        screen.target().text(btn, kAlignNames[k], smallCenterBlack);
      }
      screen.frame().hit(btn, ACTION_SETTING, static_cast<int16_t>(20 + k), fui::InputTouch);
    }
  }
  curY += 48;

  // Hàng 4: Phông chữ (Nút mở chọn phông)
  {
    const fui::Rect fontBtn{contentRect.x, curY, contentRect.width, 40};
    screen.target().stroke(fontBtn, fui::Paint::solid(fui::Color::Black), 1, 8);

    char fontBuf[64];
    snprintf(fontBuf, sizeof(fontBuf), "Ph\u00f4ng ch\u1eef: %s",
             (model_.fontName && model_.fontName[0]) ? model_.fontName : "M\u1eb7c \u0111\u1ecbnh");
    fui::TextStyle leftText = tokens.smallText;
    leftText.align = fui::TextAlign::Left;
    screen.target().text(fontBtn.inset(fui::Insets{8, 12, 6, 24}), fontBuf, leftText);
    screen.target().text(fui::Rect{static_cast<int16_t>(fontBtn.right() - 24), static_cast<int16_t>(curY + 6), 16, 28},
                        ">", centerBold);
    screen.frame().hit(fontBtn, ACTION_SETTING, 30, fui::InputTouch);
  }
}

void ReaderToolbarUi::buildDrawerTab1(UiScreen& screen, const fui::Rect& contentRect) {
  const auto& tokens = screen.theme();

  fui::TextStyle titleStyle = tokens.bodyText;
  titleStyle.bold = true;
  titleStyle.align = fui::TextAlign::Center;

  fui::TextStyle centerBold = tokens.bodyText;
  centerBold.bold = true;
  centerBold.align = fui::TextAlign::Center;

  int16_t curY = contentRect.y;

  // Dòng 1: Tên chương hiện tại
  if (model_.chapterTitle) {
    screen.target().text(fui::Rect{contentRect.x, curY, contentRect.width, 28}, model_.chapterTitle, titleStyle);
  }
  curY += 36;

  // Dòng 2: Thanh trượt tiến độ Scrub track
  {
    const int16_t trackX = static_cast<int16_t>(contentRect.x + 12);
    const int16_t trackW = static_cast<int16_t>(contentRect.width - 24);
    const int16_t trackY = static_cast<int16_t>(curY + 14);

    screen.target().fill(fui::Rect{trackX, trackY, trackW, 3}, fui::Paint::solid(fui::Color::Black));

    const int permille = std::clamp(model_.progressPermille, 0, 1000);
    const int16_t knobX = static_cast<int16_t>(trackX + (static_cast<int32_t>(trackW) * permille) / 1000);
    screen.target().fill(fui::Rect{static_cast<int16_t>(knobX - 8), static_cast<int16_t>(trackY - 7), 16, 16},
                        fui::Paint::solid(fui::Color::Black), 8);

    // Hit target cho drag / tap
    screen.frame().hit(fui::Rect{contentRect.x, curY, contentRect.width, 32}, ACTION_SCRUB, 0,
                      fui::InputTouch | fui::InputDrag);
  }
  curY += 46;

  // Dòng 3: Hai nút chuyển chương
  {
    const int16_t btnW = static_cast<int16_t>((contentRect.width - 14) / 2);

    const fui::Rect prevBtn{contentRect.x, curY, btnW, 44};
    screen.target().stroke(prevBtn, fui::Paint::solid(fui::Color::Black), 1, 8);
    screen.target().text(prevBtn, "<  Ch\u01b0\u01a1ng tr\u01b0\u1edbc", centerBold);
    screen.frame().hit(prevBtn, ACTION_NAV, 0, fui::InputTouch);

    const fui::Rect nextBtn{static_cast<int16_t>(contentRect.x + btnW + 14), curY, btnW, 44};
    screen.target().stroke(nextBtn, fui::Paint::solid(fui::Color::Black), 1, 8);
    screen.target().text(nextBtn, "Ch\u01b0\u01a1ng sau  >", centerBold);
    screen.frame().hit(nextBtn, ACTION_NAV, 1, fui::InputTouch);
  }
  curY += 56;

  // Dòng 4: Thông tin số trang & tiến độ sách
  if (model_.pageInfo) {
    fui::TextStyle infoStyle = tokens.smallText;
    infoStyle.align = fui::TextAlign::Center;
    screen.target().text(fui::Rect{contentRect.x, curY, contentRect.width, 26}, model_.pageInfo, infoStyle);
  }
}

void ReaderToolbarUi::buildDrawerTab2(UiScreen& screen, const fui::Rect& contentRect) {
  const auto& tokens = screen.theme();

  fui::TextStyle centerBold = tokens.bodyText;
  centerBold.bold = true;
  centerBold.align = fui::TextAlign::Center;

  fui::TextStyle itemStyle = tokens.smallText;

  constexpr int kRowsPerPage = 4;
  const int count = std::max(0, model_.itemCount);
  const int totalPages = (count + kRowsPerPage - 1) / kRowsPerPage;
  const int curPage = std::clamp(model_.tocPage, 0, std::max(0, totalPages - 1));

  int16_t curY = contentRect.y;

  // 4 dòng chương
  for (int i = 0; i < kRowsPerPage; ++i) {
    const int idx = curPage * kRowsPerPage + i;
    const fui::Rect rowRect{contentRect.x, curY, contentRect.width, 38};

    if (idx < count) {
      screen.target().stroke(rowRect, fui::Paint::solid(fui::Color::Black), 1, 6);
      std::string text = model_.rowText ? model_.rowText(idx) : "";
      screen.target().text(rowRect.inset(fui::Insets{6, 12, 6, 12}), text.c_str(), itemStyle);
      screen.frame().hit(rowRect, ACTION_ROW, static_cast<int16_t>(idx), fui::InputTouch);
    }
    curY += 44;
  }

  // Dưới cùng: Nút Trang trước / Trang sau
  curY = static_cast<int16_t>(contentRect.bottom() - 40);
  const fui::Rect prevBtn{contentRect.x, curY, 110, 36};
  screen.target().stroke(prevBtn, fui::Paint::solid(fui::Color::Black), 1, 6);
  screen.target().text(prevBtn, "< Tr\u01b0\u1edbc", centerBold);
  screen.frame().hit(prevBtn, ACTION_NAV, 2, fui::InputTouch);

  char pageBuf[32];
  snprintf(pageBuf, sizeof(pageBuf), "Trang %d / %d", curPage + 1, totalPages > 0 ? totalPages : 1);
  screen.target().text(fui::Rect{static_cast<int16_t>(contentRect.x + 115), static_cast<int16_t>(curY + 4),
                                static_cast<int16_t>(contentRect.width - 230), 28},
                      pageBuf, centerBold);

  const fui::Rect nextBtn{static_cast<int16_t>(contentRect.right() - 110), curY, 110, 36};
  screen.target().stroke(nextBtn, fui::Paint::solid(fui::Color::Black), 1, 6);
  screen.target().text(nextBtn, "Sau >", centerBold);
  screen.frame().hit(nextBtn, ACTION_NAV, 3, fui::InputTouch);
}

void ReaderToolbarUi::buildDrawerTab3(UiScreen& screen, const fui::Rect& contentRect) {
  const auto& tokens = screen.theme();

  fui::TextStyle centerBold = tokens.bodyText;
  centerBold.bold = true;
  centerBold.align = fui::TextAlign::Center;

  int16_t curY = contentRect.y;

  // Nút 1: Về Màn hình chính (To nổi bật)
  {
    const fui::Rect homeBtn{contentRect.x, curY, contentRect.width, 50};
    screen.target().fill(homeBtn, fui::Paint::solid(fui::Color::Black), 8);
    fui::TextStyle homeText = centerBold;
    homeText.color = fui::Color::White;
    screen.target().text(homeBtn, "V\u1ec1 M\u00e0n h\u00ecnh ch\u00ednh", homeText);
    screen.frame().hit(homeBtn, ACTION_NAV, 4, fui::InputTouch);
  }
  curY += 62;

  // Hàng 2: Thống kê đọc & Đổi nền
  {
    const int16_t halfW = static_cast<int16_t>((contentRect.width - 12) / 2);

    const fui::Rect statsBtn{contentRect.x, curY, halfW, 44};
    screen.target().stroke(statsBtn, fui::Paint::solid(fui::Color::Black), 1, 8);
    screen.target().text(statsBtn, "Th\u1ed1ng k\u00ea \u0111\u1ecdc", centerBold);
    screen.frame().hit(statsBtn, ACTION_NAV, 5, fui::InputTouch);

    const fui::Rect nightBtn{static_cast<int16_t>(contentRect.x + halfW + 12), curY, halfW, 44};
    screen.target().stroke(nightBtn, fui::Paint::solid(fui::Color::Black), 1, 8);
    screen.target().text(nightBtn, model_.isNightMode ? "\u0110\u1ed5i N\u1ec1n S\u00e1ng" : "\u0110\u1ed5i N\u1ec1n T\u1ed1i", centerBold);
    screen.frame().hit(nightBtn, ACTION_NAV, 6, fui::InputTouch);
  }
  curY += 56;

  // Hàng 3: Đèn nền
  {
    const fui::Rect lightBtn{contentRect.x, curY, contentRect.width, 44};
    screen.target().stroke(lightBtn, fui::Paint::solid(fui::Color::Black), 1, 8);
    screen.target().text(lightBtn, "\u0110\u00e8n n\u1ec1n (V\u00e0ng / Tr\u1eafng)", centerBold);
    screen.frame().hit(lightBtn, ACTION_NAV, 7, fui::InputTouch);
  }
}

// The Contents / Text / More row: three equal slots, an icon centred in each,
// the active one inside an outline pill (the theme's control radius). Each
// slot is registered as one tap target, so the row stays light (no filled
// tiles, no labels -- the glyphs carry the meaning).
void ReaderToolbarUi::buildToolRow(UiScreen& screen, const fui::LayoutAnchor anchor, const int16_t sideInset) {
  const auto& tokens = screen.theme();
  const fui::BitmapRef icons[kToolCount] = {fui::bitmapFromIcon(icon_reader_contents_24),
                                            fui::bitmapFromIcon(icon_reader_text_24),
                                            fui::bitmapFromIcon(icon_reader_more_24)};
  // sideInset absorbs the difference between the two hosts' content bands
  // (the toolbar's is spaceLg-inset, the panel's is full width): the slots
  // must land on the same x either way, or the icons jump when a tap swaps
  // the toolbar for a panel.
  const fui::Rect row = screen.take(anchor, kToolRowH).inset(fui::Insets{0, sideInset, 0, sideInset});
  const int16_t slotW = static_cast<int16_t>(row.width / kToolCount);
  // Theme radius as-is (the frontlight panel pattern); the fill clamps to
  // the shape's own height so round themes cannot overshoot.
  const uint8_t pillRadius = tokens.controlRadius;
  for (int i = 0; i < kToolCount; ++i) {
    const fui::Rect slot{static_cast<int16_t>(row.x + slotW * i), row.y, slotW, row.height};
    if (i == model_.activeTool) {
      screen.target().stroke(slot.inset(fui::Insets{4, kToolPillInset, 4, kToolPillInset}),
                             fui::Paint::solid(fui::Color::Black), 2, pillRadius);
    }
    const fui::Rect iconRect{static_cast<int16_t>(slot.x + (slot.width - 24) / 2),
                             static_cast<int16_t>(slot.y + (slot.height - 24) / 2), 24, 24};
    screen.target().bitmap(iconRect, icons[i], fui::BitmapMode::Center);
    screen.frame().hit(slot, ACTION_TOOL, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void ReaderToolbarUi::buildToolbar(UiScreen& screen) {
  const auto& tokens = screen.theme();

  // Sheet height from its content: scrub row, meta line, tool row, and the
  // air between them (a full spaceLg under the scrub row, so the progress
  // track and its chapter buttons don't crowd the meta line).
  const int16_t metaH = screen.target().lineHeight(tokens.smallText.font);
  const int16_t contentH = static_cast<int16_t>(tokens.spaceMd + kScrubButton + tokens.spaceLg + metaH +
                                                tokens.spaceSm + kToolRowH + tokens.spaceSm);
  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;
  // Grabber air matches the frontlight panel's card language (spaceLg around
  // the grabber, spaceMd more toward the free edge) so the two sheets read as
  // the same family.
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  screen.sheet(sheetProps, static_cast<int16_t>(contentH + grabberBand));
  screen.insetContent(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
  screen.spacer(tokens.spaceMd);

  // Scrub row: < [progress track + knob: tap/drag to jump] >
  {
    const fui::Rect band = screen.takeTop(kScrubButton, tokens.spaceLg);
    stepProps_.label = nullptr;
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_back_24);
    stepProps_.action = ACTION_PREV;
    stepProps_.inputMask = fui::InputTouch;
    stepProps_.styles.explicitlySet = true;
    stepProps_.styles.normal.background = fui::Paint::solid(fui::Color::White);
    stepProps_.styles.normal.foreground = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.border = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.normal.borderWidth = 1;
    stepProps_.styles.normal.radius = tokens.controlRadius;
    stepProps_.styles.selected = stepProps_.styles.normal;
    stepProps_.styles.focused = stepProps_.styles.normal;
    stepProps_.styles.disabled = stepProps_.styles.normal;
    stepProps_.styles.active = stepProps_.styles.normal;
    stepProps_.styles.active.background = fui::Paint::solid(fui::Color::Black);
    stepProps_.styles.active.foreground = fui::Paint::solid(fui::Color::White);
    screen.button(stepProps_, fui::Rect{band.x, band.y, kScrubButton, kScrubButton});
    stepProps_.icon = fui::bitmapFromIcon(icon_reader_next_24);
    stepProps_.action = ACTION_NEXT;
    screen.button(stepProps_,
                  fui::Rect{static_cast<int16_t>(band.right() - kScrubButton), band.y, kScrubButton, kScrubButton});

    // Progress: a 2px track with a solid round knob at the book position (the
    // fill edge IS the handle, so nothing else is drawn), the scrub hit rect
    // spanning the whole band height so a finger a little off the line still
    // scrubs. Tap-to-jump / drag arrive as dragPermille along this rect.
    const int16_t trackX = static_cast<int16_t>(band.x + kScrubButton + kScrubGap);
    const int16_t trackW = static_cast<int16_t>(band.width - 2 * (kScrubButton + kScrubGap));
    const int16_t trackY = static_cast<int16_t>(band.y + band.height / 2);
    const fui::Paint ink = fui::Paint::solid(fui::Color::Black);
    screen.target().fill(fui::Rect{trackX, static_cast<int16_t>(trackY - 1), trackW, 2}, ink);
    const int permille = std::clamp(model_.progressPermille, 0, 1000);
    const int16_t knobX = static_cast<int16_t>(trackX + (static_cast<int32_t>(trackW - 1) * permille) / 1000);
    screen.target().fill(fui::Rect{static_cast<int16_t>(knobX - kScrubKnob / 2),
                                   static_cast<int16_t>(trackY - kScrubKnob / 2), kScrubKnob, kScrubKnob},
                         ink, static_cast<uint8_t>(kScrubKnob / 2));
    screen.frame().hit(fui::Rect{trackX, band.y, trackW, band.height}, ACTION_SCRUB, 0,
                       fui::InputTouch | fui::InputDrag);
  }

  // Meta line: chapter title (left), chapter page / book percent (right).
  {
    const fui::Rect line = screen.takeTop(metaH, tokens.spaceSm);
    fui::TextStyle titleStyle = tokens.smallText;
    titleStyle.bold = true;
    fui::TextStyle infoStyle = tokens.smallText;
    infoStyle.align = fui::TextAlign::Right;
    const int16_t infoW =
        model_.pageInfo ? screen.target().measureText(infoStyle.font, model_.pageInfo, infoStyle).width : 0;
    const fui::Rect titleRect{line.x, line.y, static_cast<int16_t>(line.width - infoW - tokens.spaceMd), line.height};
    if (model_.chapterTitle) screen.target().text(titleRect, model_.chapterTitle, titleStyle);
    if (model_.pageInfo) screen.target().text(line, model_.pageInfo, infoStyle);
  }

  buildToolRow(screen, fui::LayoutAnchor::Top, 0);  // content band already spaceLg-inset
}

void ReaderToolbarUi::buildPanel(UiScreen& screen) {
  const auto& tokens = screen.theme();
  const fui::Rect safe = screen.frame().safeRect();

  fui::SheetProps sheetProps;
  sheetProps.anchor = fui::SheetEdge::Bottom;
  sheetProps.dismissAction = ACTION_DISMISS;  // tap the page above the sheet = back to the toolbar
  // Same grabber air as the toolbar sheet / frontlight panel.
  sheetProps.grabberMargin = tokens.spaceLg;
  sheetProps.grabberInset = static_cast<int16_t>(tokens.spaceLg + tokens.spaceMd);

  // Size the sheet to an exact number of list rows instead of a fixed screen
  // share: a percentage leaves up to a row's height of dead space above the
  // switcher, and a short list (Text, More) leaves empty row slots. Chrome =
  // everything in the sheet that is not the list; the row count starts from
  // the target share, grows one row when that still fits the cap, and shrinks
  // to the item count when the list is shorter than the space.
  const int16_t titleH = screen.target().lineHeight(tokens.titleText.font);
  const int16_t rowH =
      model_.denseRows ? static_cast<int16_t>(UITheme::getInstance().getMetrics().listRowHeight) : tokens.rowHeight;
  const int16_t rowGap = model_.denseRows ? tokens.listRowGap : std::max(tokens.listRowGap, tokens.listTouchRowGap);
  const int16_t rowStride = static_cast<int16_t>(rowH + rowGap);
  const int16_t grabberBand =
      static_cast<int16_t>(sheetProps.grabberMargin + sheetProps.grabberHeight + sheetProps.grabberInset);
  const int16_t chrome =
      static_cast<int16_t>(grabberBand + titleH + tokens.spaceMd + tokens.spaceSm + kToolRowH + tokens.spaceSm);
  const bool landscape = safe.width > safe.height;
  const int16_t target =
      static_cast<int16_t>((safe.height * (landscape ? kLandscapePanelHeightPercent : kPanelHeightPercent)) / 100);
  const int16_t cap =
      static_cast<int16_t>((safe.height * (landscape ? kLandscapePanelHeightPercent : kPanelHeightMaxPercent)) / 100);
  int sheetRows = (target - chrome + rowGap) / rowStride;
  if (static_cast<int16_t>(chrome + (sheetRows + 1) * rowStride - rowGap) <= cap) ++sheetRows;
  if (model_.itemCount > 0 && sheetRows > model_.itemCount) sheetRows = model_.itemCount;
  if (sheetRows < 1) sheetRows = 1;
  screen.sheet(sheetProps, static_cast<int16_t>(chrome + sheetRows * rowStride - rowGap));
  // No blanket side inset: Screen::list() draws in the content band, and the
  // scroll track must reach the sheet's edge like a full-screen list's does.
  // The title insets itself; the rows inset via rowInset below.

  // Title line: panel name left, page position right when the list spans pages.
  {
    fui::TextStyle titleStyle = tokens.titleText;
    titleStyle.bold = true;
    const fui::Rect line =
        screen.takeTop(titleH, tokens.spaceMd).inset(fui::Insets{0, tokens.spaceLg, 0, tokens.spaceLg});
    screen.target().text(line, model_.panelTitle, titleStyle);
    // Filled in below once the viewport is known; reserve the rect now.
    pageIndicatorRect_ = line;
  }

  // Switcher row along the sheet's bottom edge; the list takes what is left.
  screen.spacer(tokens.spaceSm, fui::LayoutAnchor::Bottom);
  buildToolRow(screen, fui::LayoutAnchor::Bottom, tokens.spaceLg);  // full-width band
  screen.spacer(tokens.spaceSm, fui::LayoutAnchor::Bottom);

  listProps_.count = static_cast<uint16_t>(std::max(0, model_.itemCount));
  listProps_.action = ACTION_ROW;
  listProps_.inputMask = fui::InputTouch;  // physical buttons stay with the reader
  listProps_.rowHeight = rowH;
  listProps_.rowGap = rowGap;
  listProps_.toggleCheckbox = true;
  listProps_.toggleWidth = 28;
  listProps_.toggleHeight = 28;
  // The label column starts flush with the panel title (no list-side padding
  // on top of the sheet's own inset). Body-size text: small reads condensed
  // and the taller row doubles as the tap target.
  listProps_.labelText = tokens.bodyText;
  listProps_.sidePadding = 0;
  // The list band spans the sheet's full width -- the scroll track hugs the
  // panel edge (theme bezel inset included) exactly like a full-screen list.
  // rowInset pulls the rows back to the title's spaceLg alignment.
  listProps_.rowInset = tokens.spaceLg;
  const fui::Rect listRect = screen.body();
  const int count = std::max(0, model_.itemCount);
  // The nav owns selection + viewport (same fui::ListNav idiom as the list
  // menu screens). A shown cursor re-follows into view on every build; a
  // hidden one (-1, touch) leaves the viewport where scrolling put it.
  nav_.selected = std::clamp(model_.selectedIndex, -1, count - 1);
  nav_.followOnBuild = nav_.selected >= 0;
  nav_.followPending = false;
  nav_.syncToProps(listRect, listProps_.rowHeight, rowGap, count, listProps_);

  // Materialise only the visible window of rows.
  const int windowCount = std::min({nav_.visibleRows, count - nav_.top, kMaxWindow});
  for (int i = 0; i < windowCount; ++i) {
    const int index = nav_.top + i;
    windowLabels_[i] = model_.rowText ? model_.rowText(index) : std::string();
    windowValues_[i] = model_.rowValue ? model_.rowValue(index) : std::string();
    fui::ListItem item;
    item.label = windowLabels_[i].c_str();
    item.value = windowValues_[i].empty() ? nullptr : windowValues_[i].c_str();
    if (model_.rowCheckbox) model_.rowCheckbox(model_.rowCheckboxContext, index, item);
    item.actionValue = static_cast<int16_t>(index);
    windowItems_[i] = item;
  }
  listProps_.items = windowItems_;
  listProps_.itemsWindowFirst = static_cast<uint16_t>(nav_.top);
  listProps_.itemsWindowCount = static_cast<uint16_t>(std::max(0, windowCount));
  listProps_.valueText = tokens.bodyText;
  listProps_.valueText.bold = true;
  if (count > 0) {
    screen.list(listProps_);
  }

  const int pageRows = nav_.pageRows();
  const int totalPages = pageRows > 0 ? (count + pageRows - 1) / pageRows : 0;
  if (totalPages > 1) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", nav_.top / pageRows + 1, totalPages);
    fui::TextStyle pageStyle = tokens.smallText;
    pageStyle.align = fui::TextAlign::Right;
    screen.target().text(pageIndicatorRect_, buf, pageStyle);
  }
}
