#include "BottomDockBar.h"

#include <GfxRenderer.h>
#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "components/icons/listIcons.h"

namespace BottomDockBar {

namespace {
bool s_globalCollapsed = false;
bool s_initialized = false;
uint32_t s_lastActiveTime = 0;

void drawDockIcon(GfxRenderer& renderer, const freeink::Icon& icon, int x, int y, bool inverted) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    for (int col = 0; col < icon.w; ++col) {
      const uint8_t byte = icon.bits[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) {
        renderer.drawPixel(x + col, y + row, !inverted);
      }
    }
  }
}
}  // namespace

bool isGlobalCollapsed() {
  if (!s_initialized) {
    s_globalCollapsed = (SETTINGS.autoHideDock != 0);
    s_initialized = true;
  }
  return s_globalCollapsed;
}

void setGlobalCollapsed(bool collapsed) {
  s_globalCollapsed = collapsed;
  s_initialized = true;
}

void render(GfxRenderer& renderer, Tab activeTab, bool isCollapsed) {
  static const freeink::Icon* const ICONS[] = {&icon_book_24, &icon_library_24, &icon_blocks_24, &icon_settings_24};
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (isCollapsed) {
    // 1. Trạng thái COLLAPSED: Xóa sạch dải đáy và vẽ thanh Indicator mỏng Neo-Brutalist
    renderer.fillRect(0, BAR_Y - 4, screenW, screenH - (BAR_Y - 4), false);
    renderer.fillRoundedRect(BAR_X, BAR_Y, BAR_W, BAR_H, BAR_RADIUS, Color::Black);
    return;
  }

  // 2. Trạng thái EXPANDED: Đảo nổi Neo-Brutalist (4 tab icon đầy đủ)
  // Xóa sạch vùng đáy xung quanh đảo nổi
  renderer.fillRect(0, ISLAND_Y - 6, screenW, screenH - (ISLAND_Y - 6), false);

  // Bóng đổ cứng Neo-Brutalist (Hard Drop Shadow đen đặc 3px dịch xuống phải)
  renderer.fillRoundedRect(ISLAND_MARGIN_X + 3, ISLAND_Y + 3, ISLAND_W, ISLAND_H, ISLAND_RADIUS, Color::Black);

  // Thân Đảo nổi: Nền trắng tinh khôi + Viền đen đôi 2px sắc bén
  renderer.fillRoundedRect(ISLAND_MARGIN_X, ISLAND_Y, ISLAND_W, ISLAND_H, ISLAND_RADIUS, Color::White);
  renderer.drawRoundedRect(ISLAND_MARGIN_X, ISLAND_Y, ISLAND_W, ISLAND_H, 2, ISLAND_RADIUS, true);

  // Vẽ 4 Tab icon
  const int activeIndex = static_cast<int>(activeTab);
  for (int i = 0; i < 4; ++i) {
    const int tabX = ISLAND_MARGIN_X + i * TAB_W;
    const bool isSelected = (i == activeIndex);

    if (isSelected) {
      // Tab Active: Nút Pill bo góc cơ học ôm trọn icon ở chính giữa
      constexpr int pillW = 72;
      constexpr int pillH = 42;
      const int pillX = tabX + (TAB_W - pillW) / 2;
      const int pillY = ISLAND_Y + (ISLAND_H - pillH) / 2;
      constexpr int pillRadius = 12;

      renderer.fillRoundedRect(pillX, pillY, pillW, pillH, pillRadius, Color::Black);

      const int iconX = pillX + (pillW - 24) / 2;
      const int iconY = pillY + (pillH - 24) / 2;
      drawDockIcon(renderer, *ICONS[i], iconX, iconY, true);
    } else {
      // Tab Inactive: Icon Lucide 24x24 màu đen căn chính giữa ô tab
      const int iconX = tabX + (TAB_W - 24) / 2;
      const int iconY = ISLAND_Y + (ISLAND_H - 24) / 2;
      drawDockIcon(renderer, *ICONS[i], iconX, iconY, false);

      // Vách ngăn dither mảnh giữa 2 tab inactive liền kề
      if (i < 3 && (i + 1) != activeIndex) {
        const int sepX = tabX + TAB_W - 1;
        for (int y = ISLAND_Y + 16; y < ISLAND_Y + 40; ++y) {
          if ((y & 1) == 0) {
            renderer.drawPixel(sepX, y, true);
          }
        }
      }
    }
  }
}

bool handleTouch(const MappedInputManager& mappedInput, Tab activeTab, bool& isCollapsed, bool& needUpdate) {
  int touchX = 0, touchY = 0;
  if (!mappedInput.wasScreenTapped(touchX, touchY)) {
    return false;
  }

  if (isCollapsed) {
    // Khi đang thu gọn: Chạm vào vùng trung tâm đáy (y >= 755 và x nằm trong khoảng 96..384) để bung mở Dock
    // Giúp các hàng sách cuối danh sách và 2 lề không bị cấn nhầm lệnh bung dock
    if (touchY >= 755 && touchX >= 96 && touchX <= 384) {
      isCollapsed = false;
      setGlobalCollapsed(false);
      resetAutoHideTimer();
      needUpdate = true;
      return true;
    }
    return false;
  }

  // Khi đang mở rộng: Kiểm tra xem có chạm vào phạm vi Đảo nổi không
  if (touchY >= ISLAND_Y - 4 && touchY <= 800 &&
      touchX >= ISLAND_MARGIN_X && touchX <= ISLAND_MARGIN_X + ISLAND_W) {
    resetAutoHideTimer();
    int clickedIndex = (touchX - ISLAND_MARGIN_X) / TAB_W;
    clickedIndex = std::max(0, std::min(3, clickedIndex));

    if (clickedIndex != static_cast<int>(activeTab)) {
      switch (static_cast<Tab>(clickedIndex)) {
        case Tab::Home:
          activityManager.goHome();
          break;
        case Tab::Library:
          activityManager.goToLibrary();
          break;
        case Tab::Apps:
          activityManager.goToPlugins(false);
          break;
        case Tab::Settings:
          activityManager.goToSettings();
          break;
      }
    }
    return true;  // Đã tiêu thụ sự kiện chạm trên thanh Dock
  }

  return false;
}

bool handleSwipe(MappedInputManager::SwipeDir swipe, bool& isCollapsed, int startX, int startY) {
  if (isCollapsed) {
    // Khi đang thu gọn: Chỉ khi vuốt ngón tay lên từ vùng vạch chỉ báo sát đáy (startY >= 730) mới bung mở Dock.
    // Vuốt ở giữa màn hình (để cuộn sách, đọc danh sách) tuyệt đối không được tự động hiện Dock.
    if (swipe == MappedInputManager::SwipeDir::Up && startY >= 730) {
      if (startX >= 0 && (startX < 100 || startX > 380)) {
        return false;
      }
      isCollapsed = false;
      setGlobalCollapsed(false);
      resetAutoHideTimer();
      return true;
    }
  } else {
    // Khi đang mở rộng (hiện 4 tab):
    // 1. Vuốt ngón tay xuống (SwipeDir::Down) từ vùng dock (startY >= 680) -> Thu gọn dock
    if (swipe == MappedInputManager::SwipeDir::Down && (startY < 0 || startY >= 680)) {
      isCollapsed = true;
      setGlobalCollapsed(true);
      resetAutoHideTimer();
      return true;
    }
    // 2. Vuốt ngón tay lên (SwipeDir::Up) khi cuộn danh sách nếu bật chế độ tự động ẩn
    if (swipe == MappedInputManager::SwipeDir::Up && SETTINGS.autoHideDock != 0) {
      isCollapsed = true;
      setGlobalCollapsed(true);
      return true;
    }
  }
  return false;
}

void resetAutoHideTimer() {
  s_lastActiveTime = millis();
}

bool checkAutoHide(bool& isCollapsed, bool force) {
  // Nếu dock đã thu gọn, hoặc không bật tự ẩn và không bị ép buộc (force)
  if (isCollapsed || (!force && SETTINGS.autoHideDock == 0)) {
    s_lastActiveTime = 0;
    return false;
  }

  const uint32_t now = millis();
  if (s_lastActiveTime == 0) {
    s_lastActiveTime = now;
    return false;
  }

  if (now - s_lastActiveTime >= AUTO_HIDE_TIMEOUT_MS) {
    isCollapsed = true;
    setGlobalCollapsed(true);
    s_lastActiveTime = 0;
    return true;
  }

  return false;
}

}  // namespace BottomDockBar
