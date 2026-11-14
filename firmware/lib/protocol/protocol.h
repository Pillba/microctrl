#pragma once

// Pi <-> MCU frame codec, see protocol/protocol.md. Pure C++ (no Arduino) so it is unit tested
// on the host against protocol/test_vectors/frame_vectors.json.
//
// Frame: AA 55 | message_type | payload_length | payload | CRC-8/MAXIM(type, length, payload)

#include <stdint.h>

namespace protocol
{
constexpr uint8_t SYNC_0 = 0xAA;
constexpr uint8_t SYNC_1 = 0x55;
constexpr uint8_t MAX_PAYLOAD_LENGTH = 64;
constexpr uint8_t FRAME_OVERHEAD = 5;  // sync(2) + type + length + crc
constexpr uint8_t MAX_FRAME_LENGTH = MAX_PAYLOAD_LENGTH + FRAME_OVERHEAD;

enum MessageType : uint8_t
{
    MSG_DISPENSE_CMD = 0x10,
    MSG_DISPENSE_RESULT = 0x11,
    MSG_BENCH_CMD = 0x80,     // production answers only the scale calibration commands below
    MSG_BENCH_RESULT = 0x81,
    MSG_ERROR = 0xFF,
};

constexpr uint8_t REQUEST_ID_LENGTH = 4;
constexpr uint8_t DISPENSE_CMD_LENGTH = 6;     // request_id[4] slot quantity
constexpr uint8_t DISPENSE_RESULT_LENGTH = 7;  // request_id[4] slot result_code confirmed_quantity
constexpr uint8_t BENCH_CMD_LENGTH = 9;        // request_id[4] command arg[4, uint32 LE]
constexpr uint8_t BENCH_RESULT_LENGTH = 10;    // request_id[4] command status value[4, int32 LE]

// BENCH_CMD commands that production answers: scale calibration, the same as firmware/bench/dispense_UI.
// Every other BENCH_CMD command is ignored (no reply).
enum BenchCommandCode : uint8_t
{
    BENCH_CALIBRATE_START = 0x08,   // the scale is empty: zero it (about 10 s)
    BENCH_CALIBRATE_FINISH = 0x09,  // arg: known weight in micrograms, 0 cancels -> value: factor x 100
};

// BENCH_RESULT status byte.
enum BenchStatus : uint8_t
{
    BENCH_STATUS_OK = 0,
    BENCH_STATUS_FAILED = 1,     // the load cell (HX711) does not answer
    BENCH_STATUS_INVALID = 2,    // FINISH without START (or the MCU restarted / the session expired)
    BENCH_STATUS_BUSY = 3,       // a dispense or another calibration step is running
    BENCH_STATUS_UNSTEADY = 5,   // calibration saved, but the readings were not steady
    BENCH_STATUS_CANCELLED = 6,  // calibration cancelled, previous factor kept
    BENCH_STATUS_NO_CHANGE = 7,  // the reading did not change (nothing on the scale), nothing saved
};

uint8_t crc8Maxim(const uint8_t *data, uint8_t length);

// Writes a full frame into out. Returns the frame length, or 0 if it does not fit.
uint8_t encodeFrame(uint8_t messageType, const uint8_t *payload, uint8_t payloadLength,
                    uint8_t *out, uint8_t outSize);

struct Frame
{
    uint8_t type;
    uint8_t length;
    const uint8_t *payload;  // points into the parser; valid until the next feed()
};

// Incremental parser. Feed one byte at a time; returns true when a frame with a valid CRC
// is complete. Bad length or bad CRC drops the frame and waits for the next sync.
class FrameParser
{
public:
    bool feed(uint8_t byte, Frame &frame);
    void reset() { length_ = 0; }

private:
    uint8_t buffer_[MAX_FRAME_LENGTH];
    uint8_t length_ = 0;
};

struct DispenseCommand
{
    uint8_t requestId[REQUEST_ID_LENGTH];
    uint8_t slot;
    uint8_t quantity;
};

// Copies the payload out of the frame. False if it is not a well-formed DISPENSE_CMD.
bool decodeDispenseCommand(const Frame &frame, DispenseCommand &command);

// Writes a complete DISPENSE_RESULT frame into out (needs 12 bytes). Returns the frame length.
uint8_t encodeDispenseResult(const DispenseCommand &command, uint8_t resultCode,
                             uint8_t confirmedQuantity, uint8_t *out, uint8_t outSize);

struct BenchCommand
{
    uint8_t requestId[REQUEST_ID_LENGTH];
    uint8_t command;
    uint32_t arg;
};

// Copies the payload out of the frame. False if it is not a well-formed BENCH_CMD.
bool decodeBenchCommand(const Frame &frame, BenchCommand &command);

// Writes a complete BENCH_RESULT frame into out (needs 15 bytes). Returns the frame length.
uint8_t encodeBenchResult(const BenchCommand &command, uint8_t status, int32_t value, uint8_t *out,
                          uint8_t outSize);

enum DispatchDecision : uint8_t
{
    DISPATCH_IGNORE,          // not a dispense command, or malformed: no reply
    DISPATCH_REJECT_INVALID,  // reply INVALID_REQUEST
    DISPATCH_REJECT_BUSY,     // reply BUSY
    DISPATCH_ACCEPT,          // queue it
};

// What the MCU does with a received frame. command is filled for every decision except IGNORE.
DispatchDecision decideDispense(const Frame &frame, bool busy, DispenseCommand &command);

// The same for a calibration BENCH_CMD: IGNORE (another type, a malformed frame, or a BENCH_CMD
// command production does not implement), REJECT_BUSY, or ACCEPT. Never REJECT_INVALID.
DispatchDecision decideCalibration(const Frame &frame, bool busy, BenchCommand &command);
} // namespace protocol
