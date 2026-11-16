#pragma once

// Optional plain-text progress lines for the production firmware.
//
// Off by default: a normal build writes only frames, and every function below is an empty inline.
// Build with DEBUG=1 in the environment to turn it on (platformio.ini passes it on as PILLBA_DEBUG):
//
//     DEBUG=1 pio run -d firmware -e production -t upload
//
// A debug build writes "DBG ..." ASCII lines between frames, never inside one. ASCII never contains
// 0xAA, so the Pi's frame parsers (panel-ui, bench_menu_frames.py) skip them; bench_menu_frames.py
// prints them as "MCU> DBG ...". Upload a normal build again before handing the board to the panel.

#include <Arduino.h>

#include <dispense_sequence.h>
#include <protocol.h>

#if defined(PILLBA_DEBUG) && (PILLBA_DEBUG + 0)
#define PILLBA_DEBUG_LOG 1
#warning "DEBUG build: production writes DBG text lines between frames. Rebuild without DEBUG=1 for normal use."
#else
#define PILLBA_DEBUG_LOG 0
#endif

namespace debug_log
{
#if PILLBA_DEBUG_LOG

void boot(bool scaleReady, bool calibrated, float factor);
void dispenseCommand(const protocol::DispenseCommand &command, protocol::DispatchDecision decision);
void calibrationCommand(const protocol::BenchCommand &command, protocol::DispatchDecision decision);
void ignoredFrame(const protocol::Frame &frame);
void dispenseEvent(DispenseEvent event, const DispenseReport &report);
void dispenseResult(const protocol::DispenseCommand &command, uint8_t code, uint8_t confirmed);
void calibrationResult(const protocol::BenchCommand &command, uint8_t status, int32_t factorX100);
void calibrationExpired();

#else

inline void boot(bool, bool, float) {}
inline void dispenseCommand(const protocol::DispenseCommand &, protocol::DispatchDecision) {}
inline void calibrationCommand(const protocol::BenchCommand &, protocol::DispatchDecision) {}
inline void ignoredFrame(const protocol::Frame &) {}
inline void dispenseEvent(DispenseEvent, const DispenseReport &) {}
inline void dispenseResult(const protocol::DispenseCommand &, uint8_t, uint8_t) {}
inline void calibrationResult(const protocol::BenchCommand &, uint8_t, int32_t) {}
inline void calibrationExpired() {}

#endif
}  // namespace debug_log
