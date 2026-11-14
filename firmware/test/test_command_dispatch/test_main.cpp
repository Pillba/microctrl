// What the production firmware does with each received frame (decideDispense).

#include <unity.h>

#include <dispense_rules.h>
#include <protocol.h>

using namespace protocol;

namespace
{
Frame makeFrame(uint8_t type, const uint8_t *payload, uint8_t length)
{
    Frame frame;
    frame.type = type;
    frame.length = length;
    frame.payload = payload;
    return frame;
}
} // namespace

void test_valid_command_is_accepted_and_copied()
{
    const uint8_t payload[6] = {0x99, 0xAC, 0xA9, 0xBB, 1, 2};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 6), false, command));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, command.requestId, 4);
    TEST_ASSERT_EQUAL(1, command.slot);
    TEST_ASSERT_EQUAL(2, command.quantity);
}

void test_busy_rejects_valid_command()
{
    const uint8_t payload[6] = {1, 2, 3, 4, 6, 1};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_BUSY, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 6), true, command));
}

void test_invalid_slot_or_quantity_is_rejected_even_when_busy()
{
    const uint8_t slotZero[6] = {1, 2, 3, 4, 0, 1};
    const uint8_t slotSeven[6] = {1, 2, 3, 4, 7, 1};
    const uint8_t quantityZero[6] = {1, 2, 3, 4, 1, 0};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, slotZero, 6), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, slotSeven, 6), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, quantityZero, 6), true, command));
}

void test_other_messages_and_wrong_length_are_ignored()
{
    const uint8_t payload[7] = {1, 2, 3, 4, 1, 1, 0};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_RESULT, payload, 7), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 7), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 5), false, command));
}

void test_result_frame_matches_shared_vector()
{
    // protocol/test_vectors/frame_vectors.json "dispense result example"
    const uint8_t expected[12] = {0xAA, 0x55, 0x11, 0x07, 0x01, 0x02, 0x03, 0x04, 0x03, 0x00, 0x02, 0x85};
    DispenseCommand command = {{1, 2, 3, 4}, 3, 2};
    uint8_t out[12];
    TEST_ASSERT_EQUAL(12, encodeDispenseResult(command, dispense::RESULT_OK, 2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 12);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_command_is_accepted_and_copied);
    RUN_TEST(test_busy_rejects_valid_command);
    RUN_TEST(test_invalid_slot_or_quantity_is_rejected_even_when_busy);
    RUN_TEST(test_other_messages_and_wrong_length_are_ignored);
    RUN_TEST(test_result_frame_matches_shared_vector);
    return UNITY_END();
}
