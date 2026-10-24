#pragma once

#include <cstdint>

#include "MappedInputManager.h"

class GfxRenderer;

namespace BottomDockBar {

enum class Tab : uint8_t {
  Home = 0,
  Library = 1,
  Apps = 2,
  Settings = 3
};

// Kích thước và thông số hình học của Đảo nổi Neo-Brutalist
constexpr int ISLAND_MARGIN_X = 24;
constexpr int ISLAND_W = 432;           // 480 - 2 * 24
constexpr int ISLAND_H = 50;
constexpr int ISLAND_BOTTOM_GAP = 12;
constexpr int ISLAND_Y = 800 - ISLAND_BOTTOM_GAP - ISLAND_H;  // 738px
constexpr int ISLAND_RADIUS = 12;
constexpr int TAB_W = ISLAND_W / 4;     // 108px

// Kích thước của thanh mỏng khi COLLAPSED (Indicator bar)
// Thanh mảnh 112x4px thanh thoát, bằng bề rộng 1 Tab của Dock, cách đáy 12px
constexpr int BAR_W = 112;
constexpr int BAR_H = 4;
constexpr int BAR_RADIUS = 2;
constexpr int BAR_BOTTOM_GAP = 12;
constexpr int BAR_X = (480 - BAR_W) / 2;                      // 184px
constexpr int BAR_Y = 800 - BAR_BOTTOM_GAP - BAR_H;           // 784px

// Chiều cao lề dưới cần chừa cho danh sách nội dung:
// Khi thu gọn chừa 32px (danh sách dừng ở 768px, cách thanh bar 16px đệm an toàn)
constexpr int RESERVED_EXPANDED = 66;
constexpr int RESERVED_COLLAPSED = 32;

inline int getBottomReservedHeight(bool isCollapsed) {
  return isCollapsed ? RESERVED_COLLAPSED : RESERVED_EXPANDED;
}

// Lấy / đặt trạng thái thu gọn toàn cục
bool isGlobalCollapsed();
void setGlobalCollapsed(bool collapsed);

// Vẽ thanh Dock lên màn hình (tùy theo trạng thái collapsed)
void render(GfxRenderer& renderer, Tab activeTab, bool isCollapsed);

// Xử lý touch tap:
// - Khi collapsed: chạm vùng đáy (y >= 740) -> bung dock ra, trả về true
// - Khi expanded: chạm vào 1 trong 4 tab -> chuyển activity tương ứng, trả về true
// Trả về true nếu sự kiện chạm đã được tiêu thụ bởi Dock
bool handleTouch(const MappedInputManager& mappedInput, Tab activeTab, bool& isCollapsed, bool& needUpdate);

// Xử lý cử chỉ vuốt (Swipe):
// - Khi đang thu gọn: vuốt lên (SwipeDir::Up) -> mở rộng dock (isCollapsed = false)
// - Khi đang mở rộng: vuốt xuống (SwipeDir::Down) hoặc cuộn -> thu gọn dock (isCollapsed = true)
// Trả về true nếu trạng thái isCollapsed bị thay đổi
bool handleSwipe(MappedInputManager::SwipeDir swipe, bool& isCollapsed, int startY = -1);

// Thời gian chờ tự động ẩn dock (5 giây)
constexpr uint32_t AUTO_HIDE_TIMEOUT_MS = 5000;

// Reset bộ đếm giờ tự ẩn dock
void resetAutoHideTimer();

// Kiểm tra và tự động thu gọn dock nếu qua 5s không tương tác (hoặc khi force = true)
// Trả về true nếu dock vừa tự động thu gọn (cần vẽ lại màn hình)
bool checkAutoHide(bool& isCollapsed, bool force = false);

}  // namespace BottomDockBar
