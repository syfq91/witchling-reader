#pragma once
#include <cstdint>

class FramebufferUtil {
 public:
  static bool saveFramebufferAsBmp(const char* filename, const uint8_t* framebuffer, int width, int height);
};
