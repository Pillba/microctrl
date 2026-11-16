// What the production firmware does with each received frame (decideDispense, decideCalibration).

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

void test_six_byte_command_has_no_unit_weight()
{
    const uint8_t payload[6] = {1, 2, 3, 4, 1, 2};
    DispenseCommand command;
    command.unitMg = 999;
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 6), false, command));
    TEST_ASSERT_EQUAL_UINT16(0, command.unitMg);  // 0 = firmware default
}

void test_eight_byte_command_carries_unit_weight()
{
    // protocol/test_vectors/frame_vectors.json "dispense command with unit weight, slot 1, 2 pills, 650 mg each"
    const uint8_t pills[8] = {1, 2, 3, 4, 1, 2, 0x8A, 0x02};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideDispense(makeFrame(MSG_DISPENSE_CMD, pills, 8), false, command));
    TEST_ASSERT_EQUAL(1, command.slot);
    TEST_ASSERT_EQUAL(2, command.quantity);
    TEST_ASSERT_EQUAL_UINT16(650, command.unitMg);

    const uint8_t liquid[8] = {1, 2, 3, 4, 7, 5, 0x06, 0x04};  // 1030 mg per mL
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideDispense(makeFrame(MSG_DISPENSE_CMD, liquid, 8), false, command));
    TEST_ASSERT_EQUAL_UINT16(1030, command.unitMg);

    const uint8_t badSlot[8] = {1, 2, 3, 4, 8, 1, 0x8A, 0x02};
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, badSlot, 8), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_BUSY, decideDispense(makeFrame(MSG_DISPENSE_CMD, pills, 8), true, command));
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
    const uint8_t slotEight[6] = {1, 2, 3, 4, 8, 1};
    const uint8_t quantityZero[6] = {1, 2, 3, 4, 1, 0};
    const uint8_t liquidTooMuch[6] = {1, 2, 3, 4, 7, dispense::LIQUID_MAX_ML + 1};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, slotZero, 6), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, slotEight, 6), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, quantityZero, 6), true, command));
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_INVALID, decideDispense(makeFrame(MSG_DISPENSE_CMD, liquidTooMuch, 6), true, command));
}

void test_liquid_slot_is_accepted()
{
    const uint8_t liquid[6] = {9, 8, 7, 6, 7, 5};  // slot 7, 5 mL
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideDispense(makeFrame(MSG_DISPENSE_CMD, liquid, 6), false, command));
    TEST_ASSERT_EQUAL(7, command.slot);
    TEST_ASSERT_EQUAL(5, command.quantity);
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_BUSY, decideDispense(makeFrame(MSG_DISPENSE_CMD, liquid, 6), true, command));
}

void test_other_messages_and_wrong_length_are_ignored()
{
    const uint8_t payload[7] = {1, 2, 3, 4, 1, 1, 0};
    DispenseCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_RESULT, payload, 7), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 7), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_CMD, payload, 5), false, command));
    const uint8_t nine[9] = {1, 2, 3, 4, 1, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_DISPENSE_CMD, nine, 9), false, command));
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

void test_calibration_commands_are_accepted_and_decoded()
{
    // protocol/test_vectors/frame_vectors.json "calibrate finish command example, 2 g"
    const uint8_t finish[9] = {0x01, 0x02, 0x03, 0x04, 0x09, 0x80, 0x84, 0x1E, 0x00};
    const uint8_t start[9] = {0x01, 0x02, 0x03, 0x04, 0x08, 0, 0, 0, 0};
    BenchCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideCalibration(makeFrame(MSG_BENCH_CMD, finish, 9), false, command));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(finish, command.requestId, 4);
    TEST_ASSERT_EQUAL_HEX8(BENCH_CALIBRATE_FINISH, command.command);
    TEST_ASSERT_EQUAL_UINT32(2000000UL, command.arg);
    TEST_ASSERT_EQUAL(DISPATCH_ACCEPT, decideCalibration(makeFrame(MSG_BENCH_CMD, start, 9), false, command));
    TEST_ASSERT_EQUAL_HEX8(BENCH_CALIBRATE_START, command.command);
    TEST_ASSERT_EQUAL(DISPATCH_REJECT_BUSY, decideCalibration(makeFrame(MSG_BENCH_CMD, start, 9), true, command));
}

void test_other_bench_commands_and_frames_are_ignored()
{
    const uint8_t readWeight[9] = {1, 2, 3, 4, 0x07, 0, 0, 0, 0};    // dispense_UI only
    const uint8_t stepperLoop[9] = {1, 2, 3, 4, 0x01, 1, 0, 0, 0};   // dispense_UI only
    const uint8_t dispenseCmd[6] = {1, 2, 3, 4, 1, 1};
    BenchCommand command;
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideCalibration(makeFrame(MSG_BENCH_CMD, readWeight, 9), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideCalibration(makeFrame(MSG_BENCH_CMD, stepperLoop, 9), true, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideCalibration(makeFrame(MSG_BENCH_CMD, readWeight, 8), false, command));
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideCalibration(makeFrame(MSG_DISPENSE_CMD, dispenseCmd, 6), false, command));
    // and a BENCH_CMD is never mistaken for a dispense
    DispenseCommand dispenseCommand;
    TEST_ASSERT_EQUAL(DISPATCH_IGNORE, decideDispense(makeFrame(MSG_BENCH_CMD, readWeight, 9), false, dispenseCommand));
}

void test_bench_result_matches_shared_vector()
{
    // protocol/test_vectors/frame_vectors.json "calibrate finish result example, factor 420.00"
    const uint8_t expected[15] = {0xAA, 0x55, 0x81, 0x0A, 0x01, 0x02, 0x03, 0x04,
                                  0x09, 0x00, 0x10, 0xA4, 0x00, 0x00, 0xAB};
    BenchCommand command = {{1, 2, 3, 4}, BENCH_CALIBRATE_FINISH, 2000000UL};
    uint8_t out[15];
    TEST_ASSERT_EQUAL(15, encodeBenchResult(command, BENCH_STATUS_OK, 42000, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 15);
}

void test_bench_result_value_is_signed_little_endian()
{
    BenchCommand command = {{0, 0, 0, 1}, BENCH_CALIBRATE_FINISH, 0};
    uint8_t out[15];
    TEST_ASSERT_EQUAL(15, encodeBenchResult(command, BENCH_STATUS_CANCELLED, -2, out, sizeof(out)));
    TEST_ASSERT_EQUAL_HEX8(BENCH_STATUS_CANCELLED, out[9]);
    const uint8_t minusTwo[4] = {0xFE, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(minusTwo, &out[10], 4);
    TEST_ASSERT_EQUAL(0, encodeBenchResult(command, BENCH_STATUS_OK, 0, out, 14));  // too small
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_command_is_accepted_and_copied);
    RUN_TEST(test_six_byte_command_has_no_unit_weight);
    RUN_TEST(test_eight_byte_command_carries_unit_weight);
    RUN_TEST(test_busy_rejects_valid_command);
    RUN_TEST(test_invalid_slot_or_quantity_is_rejected_even_when_busy);
    RUN_TEST(test_liquid_slot_is_accepted);
    RUN_TEST(test_other_messages_and_wrong_length_are_ignored);
    RUN_TEST(test_result_frame_matches_shared_vector);
    RUN_TEST(test_calibration_commands_are_accepted_and_decoded);
    RUN_TEST(test_other_bench_commands_and_frames_are_ignored);
    RUN_TEST(test_bench_result_matches_shared_vector);
    RUN_TEST(test_bench_result_value_is_signed_little_endian);
    return UNITY_END();
}
