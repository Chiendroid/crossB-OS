#pragma once

#include <functional>
#include <string>

#include "components/UiAppHost.h"
#include "components/lists/list.h"

class GfxRenderer;
class MappedInputManager;

// FreeInkUI chrome for the toolbar reader menu (Settings -> Reader -> Reader
// Menu Style -> Toolbar), painted over the page that is already on screen:
//
//  - Toolbar: a bottom sheet holding a chapter scrub row (< capsule >) and a
//    Contents / Text / More tile row; the page above it stays untouched.
//  - Panel: a taller bottom sheet with a title, a paged row list (name left,
//    value right) and the same tile row as a switcher.
//
// EpubReaderActivity owns the state (which overlay, selection, row contents)
// and the physical-button handling; this class owns the layout, the tap
// targets FreeInkUI registers for it, and the translation of dispatched
// actions back into the small event set the reader acts on. Nothing here
// clears the screen: the page stays visible around the chrome.
class ReaderToolbarUi : public UiAppHost {
 public:
  enum class Event {
    None = 0,
    Dismiss = 1,
    Tool = 2,
    PrevChapter = 3,
    NextChapter = 4,
    Scrub = 5,
    Row = 6,
    FontSizeDec = 7,
    FontSizeInc = 8,
    Spacing = 9,
    Align = 10,
    FontPicker = 11,
    GoHome = 12,
    BookStats = 13,
    NightMode = 14,
    FrontlightControl = 15,
    TocPagePrev = 16,
    TocPageNext = 17,
  };

  struct Model {
    bool panel = false;  // false = drawer, true = standard panel fallback
    int activeTab = 0;   // 0: Chữ & Bố cục, 1: % Tiến độ, 2: ≡ Mục lục, 3: ⌂ Tiện ích

    // Tab 0 (Chữ & Bố cục)
    int fontSize = 14;
    int lineSpacing = 0;   // 0: Gọn, 1: Vừa, 2: Rộng
    int alignment = 0;     // 0: Căn đều, 1: Căn trái
    const char* fontName = nullptr;

    // Tab 1 (% Tiến độ) & Header
    const char* chapterTitle = nullptr;
    const char* pageInfo = nullptr;  // "12/40   51%"
    int progressPermille = 0;        // 0..1000

    // Tab 2 (Mục lục) & Panel fallback
    const char* panelTitle = nullptr;
    int itemCount = 0;
    int selectedIndex = -1;  // row cursor sits on; -1 = none shown
    int tocPage = 0;
    int tocTotalPages = 1;
    std::function<std::string(int)> rowText;
    std::function<std::string(int)> rowValue;
    void (*rowCheckbox)(void* ctx, int index, freeink::ui::ListItem& item) = nullptr;
    void* rowCheckboxContext = nullptr;

    // Tab 3 (Tiện ích)
    bool isNightMode = false;
    bool hasFrontlight = false;

    // Tile row (fallback)
    int activeTool = 0;
    bool denseRows = false;
  };

  struct Routed {
    Event event = Event::None;
    int value = 0;        // Tool: tool index; Row: row index
    int permille = -1;    // Scrub: 0..1000 along the track
    bool routed = false;  // the gate was open and a touch frame was routed
    int x = 0;            // touch position of the routed frame (logical px)
    int y = 0;
  };

  explicit ReaderToolbarUi(GfxRenderer& renderer);

  // Screen-entry reset + action wiring. Call once when the overlay opens.
  void begin();
  // What the next render() draws. The strings must stay alive until render()
  // returns; the row callbacks are invoked during render() for the visible rows.
  void setModel(const Model& model) { model_ = model; }
  // Paint into the framebuffer (no refresh, no clear). Run from the render task.
  void render();
  // Route one loop-task input frame; returns the action it mapped to, if any.
  Routed route(const MappedInputManager& input);

  // Panel list selection/viewport, shared with the reader's input handling.
  // The same fui::ListNav the list-menu screens use: scrollBy() pages the
  // viewport (measured page size, no-op detection), the top/selected fields
  // are the live state, and buildPanel() syncs it into the list each build.
  freeink::ui::ListNav& nav() { return nav_; }
  // Rows one page holds, measured after the first render.
  int visibleRows() const { return nav_.pageRows(); }

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  void buildDrawer(UiScreen& screen);
  void buildDrawerTabBar(UiScreen& screen, const freeink::ui::Rect& barRect);
  void buildDrawerTab0(UiScreen& screen, const freeink::ui::Rect& contentRect);
  void buildDrawerTab1(UiScreen& screen, const freeink::ui::Rect& contentRect);
  void buildDrawerTab2(UiScreen& screen, const freeink::ui::Rect& contentRect);
  void buildDrawerTab3(UiScreen& screen, const freeink::ui::Rect& contentRect);
  void buildToolbar(UiScreen& screen);
  void buildPanel(UiScreen& screen);
  void buildToolRow(UiScreen& screen, freeink::ui::LayoutAnchor anchor, int16_t sideInset);

  Model model_;
  Routed pending_;
  freeink::ui::ListNav nav_;

  // Row window materialised for the visible page only (a contents list runs to
  // hundreds of entries; labels are copied here so ListItem can point at them).
  static constexpr int kMaxWindow = 16;
  std::string windowLabels_[kMaxWindow];
  std::string windowValues_[kMaxWindow];
  freeink::ui::ListItem windowItems_[kMaxWindow];
  // fui::ButtonProps / ListProps / HeaderProps embed a 324-byte StyleSet: keep
  // them off the stack (AGENTS.md: locals stay under 256 bytes).
  freeink::ui::ButtonProps stepProps_;
  freeink::ui::ListProps listProps_;
  freeink::ui::Rect pageIndicatorRect_{};
};
