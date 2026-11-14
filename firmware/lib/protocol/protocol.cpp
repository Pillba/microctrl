#include "protocol.h"

#include <dispense_rules.h>

namespace protocol
{
uint8_t crc8Maxim(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8C : (crc >> 1);
    }
    return crc;
}

uint8_t encodeFrame(uint8_t messageType, const uint8_t *payload, uint8_t payloadLength,
                    uint8_t *out, uint8_t outSize)
{
    if (payloadLength > MAX_PAYLOAD_LENGTH) return 0;
    const uint8_t frameLength = payloadLength + FRAME_OVERHEAD;
    if (outSize < frameLength) return 0;
    out[0] = SYNC_0;
    out[1] = SYNC_1;
    out[2] = messageType;
    out[3] = payloadLength;
    for (uint8_t i = 0; i < payloadLength; i++) out[4 + i] = payload[i];
    out[frameLength - 1] = crc8Maxim(&out[2], payloadLength + 2);
    return frameLength;
}

bool FrameParser::feed(uint8_t byte, Frame &frame)
{
    if (length_ == 0 && byte != SYNC_0) return false;
    if (length_ == 1 && byte != SYNC_1)
    {
        length_ = (byte == SYNC_0) ? 1 : 0;
        return false;
    }

    buffer_[length_++] = byte;
    if (length_ < 4) return false;

    const uint8_t payloadLength = buffer_[3];
    if (payloadLength > MAX_PAYLOAD_LENGTH)
    {
        length_ = 0;
        return false;
    }
    const uint8_t frameLength = payloadLength + FRAME_OVERHEAD;
    if (length_ < frameLength) return false;

    // The frame is complete: reset before returning so a nested feed() never sees it again.
    length_ = 0;
    if (buffer_[frameLength - 1] != crc8Maxim(&buffer_[2], payloadLength + 2)) return false;
    frame.type = buffer_[2];
    frame.length = payloadLength;
    frame.payload = &buffer_[4];
    return true;
}

bool decodeDispenseCommand(const Frame &frame, DispenseCommand &command)
{
    if (frame.type != MSG_DISPENSE_CMD || frame.length != DISPENSE_CMD_LENGTH) return false;
    for (uint8_t i = 0; i < REQUEST_ID_LENGTH; i++) command.requestId[i] = frame.payload[i];
    command.slot = frame.payload[4];
    command.quantity = frame.payload[5];
    return true;
}

uint8_t encodeDispenseResult(const DispenseCommand &command, uint8_t resultCode,
                             uint8_t confirmedQuantity, uint8_t *out, uint8_t outSize)
{
    uint8_t payload[DISPENSE_RESULT_LENGTH];
    for (uint8_t i = 0; i < REQUEST_ID_LENGTH; i++) payload[i] = command.requestId[i];
    payload[4] = command.slot;
    payload[5] = resultCode;
    payload[6] = confirmedQuantity;
    return encodeFrame(MSG_DISPENSE_RESULT, payload, sizeof(payload), out, outSize);
}

DispatchDecision decideDispense(const Frame &frame, bool busy, DispenseCommand &command)
{
    if (!decodeDispenseCommand(frame, command)) return DISPATCH_IGNORE;
    if (!dispense::validRequest(command.slot, command.quantity)) return DISPATCH_REJECT_INVALID;
    if (busy) return DISPATCH_REJECT_BUSY;
    return DISPATCH_ACCEPT;
}
} // namespace protocol
