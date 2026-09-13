#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// On X4, all peripherals (RTC, fuel gauge, IMU) are absent or internal; no I2C bus needed.
class HalI2cBus {
 public:
  class Lock {
   public:
    Lock() = default;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  static void begin() {}
  static void adoptMutex(SemaphoreHandle_t = nullptr) {}
  static void ensureBusStarted() {}
};
