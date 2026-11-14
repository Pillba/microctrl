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
    MSG_ERROR = 0xFF,
};

constexpr uint8_t REQUEST_ID_LENGTH = 4;
constexpr uint8_t DISPENSE_CMD_LENGTH = 6;     // request_id[4] slot quantity
constexpr uint8_t DISPENSE_RESULT_LENGTH = 7;  // request_id[4] slot result_code confirmed_quantity

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

enum DispatchDecision : uint8_t
{
    DISPATCH_IGNORE,          // not a dispense command, or malformed: no reply
    DISPATCH_REJECT_INVALID,  // reply INVALID_REQUEST
    DISPATCH_REJECT_BUSY,     // reply BUSY
    DISPATCH_ACCEPT,          // queue it
};

// What the MCU does with a received frame. command is filled for every decision except IGNORE.
DispatchDecision decideDispense(const Frame &frame, bool busy, DispenseCommand &command);
} // namespace protocol
