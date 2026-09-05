#include "HalI2cBus.h"

#include <HalCapabilities.h>
#include <Logging.h>
#include <Wire.h>

void HalI2cBus::ensureBusStarted() {
  static bool started = false;
  if (started) return;
  started = true;

  const BoardConfig::BatteryGaugeConfig& gauge = BoardConfig::ACTIVE.batteryGauge;
  const BoardConfig::SensorsConfig& sensors = BoardConfig::ACTIVE.sensors;
  int8_t sda = BoardConfig::PIN_UNASSIGNED;
  int8_t scl = BoardConfig::PIN_UNASSIGNED;
  uint32_t hz = 400000;
  if (gauge.gaugeAddr != 0 && gauge.i2cSda != BoardConfig::PIN_UNASSIGNED) {
    sda = gauge.i2cSda;
    scl = gauge.i2cScl;
    hz = gauge.i2cHz;
  } else if (sensors.rtcAddr != 0 && sensors.i2cSda != BoardConfig::PIN_UNASSIGNED) {
    sda = sensors.i2cSda;
    scl = sensors.i2cScl;
    hz = sensors.i2cHz;
  }
  if (sda == BoardConfig::PIN_UNASSIGNED) {
    // Nothing of ours lives on I2C (X4). Leave the bus alone entirely.
    return;
  }

  Wire.begin(sda, scl, hz);
  // Short timeout: a wedged peripheral must not stall the loop task.
  Wire.setTimeOut(4);
  LOG_INF("I2C", "Bus started SDA%d/SCL%d @%luHz", sda, scl, static_cast<unsigned long>(hz));
}

