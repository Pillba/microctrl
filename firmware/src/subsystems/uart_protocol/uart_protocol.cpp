#include "uart_protocol.h"

namespace UartProtocol
{
void begin(unsigned long baud)
{
    Serial.begin(baud);
}

void poll()
{
    // Frame decoding is intentionally kept behind this boundary until the
    // canonical dispense payload is approved by both contributors.
}
} // namespace UartProtocol
