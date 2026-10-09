#pragma once

#include <cstdint>

class GfxRenderer;

// Passed by value through the loader/reader handoff. Check only between loading
// stages, while the framebuffer is available and before painting page contents.
class BookLoadingFeedback {
  uint32_t startedAt = 0;
  bool pending = false;

 public:
  static constexpr uint32_t DELAY_MS = 300;

  void start(uint32_t now) {
    startedAt = now;
    pending = true;
  }
  void finish() { pending = false; }
  bool isDue(uint32_t now) const { return pending && uint32_t(now - startedAt) >= DELAY_MS; }
  bool showIfDue(GfxRenderer& renderer);
};
