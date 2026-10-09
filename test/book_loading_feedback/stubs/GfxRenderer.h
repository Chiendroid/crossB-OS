#pragma once
class GfxRenderer {
 public:
  bool available = true;
  bool hasFrameBuffer() const { return available; }
};
