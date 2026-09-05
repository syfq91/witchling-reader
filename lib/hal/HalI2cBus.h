#pragma once

#include <BoardConfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Manages the I2C bus for the RTC and fuel gauge on ESP32-C3.
class HalI2cBus {
 public:
  // On C3 without touch, sampler reads ADC only, so RTC/gauge access on loop task needs no mutex.
  class Lock {
   public:
    Lock() = default;
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
  };

  static void begin() {}

  // Bring the I2C bus up, once, from the active board profile (for X3 BQ27220 / DS3231).
  static void ensureBusStarted();
};
