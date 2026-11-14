// Weight tolerance and request validation used by DispenseSequence.

#include <unity.h>

#include <dispense_rules.h>

using namespace dispense;

void test_tolerance_is_percent_of_expected()
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.1425, weightTolerance(0.95));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.285, weightTolerance(1.90));  // quantity 2
}

void test_tolerance_has_a_floor()
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, WEIGHT_TOLERANCE_MIN_G, weightTolerance(0.146));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, WEIGHT_TOLERANCE_MIN_G, weightTolerance(0.0));
}

void test_weight_matches_inside_band_only()
{
    TEST_ASSERT_TRUE(weightMatches(0.95, 0.95));
    TEST_ASSERT_TRUE(weightMatches(0.95 + 0.14, 0.95));
    TEST_ASSERT_TRUE(weightMatches(0.95 - 0.14, 0.95));
    TEST_ASSERT_FALSE(weightMatches(0.95 + 0.15, 0.95));
    TEST_ASSERT_FALSE(weightMatches(0.95 - 0.15, 0.95));
    TEST_ASSERT_FALSE(weightMatches(0.95, 1.90));  // one pill when two were requested
}

void test_weight_arrived_threshold()
{
    TEST_ASSERT_FALSE(weightArrived(0.0));
    TEST_ASSERT_FALSE(weightArrived(0.029));
    TEST_ASSERT_TRUE(weightArrived(0.031));
    TEST_ASSERT_TRUE(weightArrived(-0.031));  // a sign-flipped load cell still counts as a change
}

void test_request_validation()
{
    TEST_ASSERT_FALSE(validRequest(0, 1));
    TEST_ASSERT_TRUE(validRequest(1, 1));
    TEST_ASSERT_TRUE(validRequest(6, 255));
    TEST_ASSERT_FALSE(validRequest(7, 1));
    TEST_ASSERT_FALSE(validRequest(3, 0));
}

void test_result_codes_keep_existing_wire_values()
{
    // The Pi already handles 0 (success) and 3 (busy); dispense_UI.cpp sent 1 and 2.
    TEST_ASSERT_EQUAL(0, RESULT_OK);
    TEST_ASSERT_EQUAL(1, RESULT_FAILED);
    TEST_ASSERT_EQUAL(2, RESULT_INVALID_REQUEST);
    TEST_ASSERT_EQUAL(3, RESULT_BUSY);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_tolerance_is_percent_of_expected);
    RUN_TEST(test_tolerance_has_a_floor);
    RUN_TEST(test_weight_matches_inside_band_only);
    RUN_TEST(test_weight_arrived_threshold);
    RUN_TEST(test_request_validation);
    RUN_TEST(test_result_codes_keep_existing_wire_values);
    return UNITY_END();
}
