#pragma once

#include <Arduino.h>

namespace UartProtocol
{
constexpr uint8_t FRAME_SYNC_0 = 0xAA;
constexpr uint8_t FRAME_SYNC_1 = 0x55;
constexpr uint8_t DISPENSE_CMD = 0x10;
constexpr uint8_t DISPENSE_RESULT = 0x11;
constexpr uint8_t ERROR = 0xFF;
constexpr uint8_t MAX_PAYLOAD_LENGTH = 64;

void begin(unsigned long baud);
void poll();
} // namespace UartProtocol
