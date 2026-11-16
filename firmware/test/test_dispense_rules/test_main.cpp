// Weight tolerance, liquid rules and request validation used by DispenseSequence.

#include <unity.h>

#include <dispense_rules.h>

using namespace dispense;

void test_tolerance_is_percent_of_expected()
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.4275, weightTolerance(0.95));  // 45 %
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.855, weightTolerance(1.90));   // quantity 2
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.18, weightTolerance(0.40));
}

void test_tolerance_has_a_floor()
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, WEIGHT_TOLERANCE_MIN_G, weightTolerance(0.06));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, WEIGHT_TOLERANCE_MIN_G, weightTolerance(0.0));
}

void test_weight_matches_inside_band_only()
{
    TEST_ASSERT_TRUE(weightMatches(0.40, 0.40));
    TEST_ASSERT_TRUE(weightMatches(0.58, 0.40));   // exactly +45 %
    TEST_ASSERT_TRUE(weightMatches(0.22, 0.40));   // exactly -45 %
    TEST_ASSERT_FALSE(weightMatches(0.59, 0.40));
    TEST_ASSERT_FALSE(weightMatches(0.21, 0.40));
    TEST_ASSERT_TRUE(weightMatches(0.95 + 0.42, 0.95));
    TEST_ASSERT_FALSE(weightMatches(0.95 + 0.44, 0.95));
    TEST_ASSERT_FALSE(weightMatches(0.95, 1.90));  // one pill when two were requested
    TEST_ASSERT_FALSE(weightMatches(0.95 * 3, 0.95 * 2));  // one extra pill on two
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
    TEST_ASSERT_FALSE(validRequest(3, 0));
    TEST_ASSERT_FALSE(validRequest(8, 1));
    TEST_ASSERT_FALSE(validRequest(255, 1));
}

void test_liquid_request_validation()
{
    TEST_ASSERT_TRUE(isLiquidSlot(7));
    TEST_ASSERT_FALSE(isLiquidSlot(6));
    TEST_ASSERT_FALSE(validSlot(7));  // not a stepper
    TEST_ASSERT_TRUE(validRequest(7, 1));
    TEST_ASSERT_TRUE(validRequest(7, LIQUID_MAX_ML));
    TEST_ASSERT_FALSE(validRequest(7, LIQUID_MAX_ML + 1));
    TEST_ASSERT_FALSE(validRequest(7, 0));
    TEST_ASSERT_FALSE(validRequest(7, 255));
}

void test_liquid_target_and_tolerance()
{
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 5.0, liquidTargetG(5));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 6.0, liquidTargetG(5, 1.2));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.0, liquidTolerance(20.0));               // 10 %
    TEST_ASSERT_FLOAT_WITHIN(1e-6, LIQUID_TOLERANCE_MIN_G, liquidTolerance(1.0));  // floor
    TEST_ASSERT_TRUE(liquidMatches(5.4, 5.0));
    TEST_ASSERT_TRUE(liquidMatches(4.6, 5.0));
    TEST_ASSERT_FALSE(liquidMatches(5.6, 5.0));
    TEST_ASSERT_FALSE(liquidMatches(4.4, 5.0));
    TEST_ASSERT_TRUE(liquidMatches(21.9, 20.0));
    TEST_ASSERT_FALSE(liquidMatches(17.9, 20.0));
}

void test_pump_budget_grows_with_volume()
{
    TEST_ASSERT_EQUAL_UINT32(LIQUID_PRIME_MS + LIQUID_MS_PER_ML, pumpBudgetMs(1));
    TEST_ASSERT_EQUAL_UINT32(5000UL + 2000UL * 30, pumpBudgetMs(30));
    TEST_ASSERT_TRUE(pumpBudgetMs(255) > pumpBudgetMs(254));  // no overflow at the byte limit
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
    RUN_TEST(test_liquid_request_validation);
    RUN_TEST(test_liquid_target_and_tolerance);
    RUN_TEST(test_pump_budget_grows_with_volume);
    RUN_TEST(test_result_codes_keep_existing_wire_values);
    return UNITY_END();
}
