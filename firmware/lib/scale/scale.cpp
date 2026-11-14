#include "scale.h"

#include <EEPROM.h>
#include <dispense_rules.h>

bool Scale::begin()
{
    hx711_.begin(board::LOAD_CELL_DOUT_PIN, board::LOAD_CELL_SCK_PIN);
    if (EEPROM.read(board::EEPROM_ADDR_MARKER) == board::EEPROM_MARKER)
    {
        EEPROM.get(board::EEPROM_ADDR_FACTOR, factor_);
        calibrated_ = (fabs(factor_) >= 0.5 && fabs(factor_) < 1000000.0);
        if (!calibrated_) factor_ = board::DEFAULT_CALIBRATION_FACTOR;
    }
    hx711_.set_scale(factor_);
    if (!ready()) return false;
    hx711_.tare(board::STARTUP_TARE_SCANS);
    return true;
}

bool Scale::ready(unsigned long timeoutMs)
{
    return hx711_.wait_ready_timeout(timeoutMs);
}

bool Scale::waitForWeight(unsigned long timeoutMs, PollHook hook)
{
    const unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        if (hook) hook();
        if (dispense::weightArrived(hx711_.get_units(board::CHECK_SCANS))) return true;
    }
    return false;
}

bool Scale::tryGrams(float &grams, uint8_t scans, unsigned long timeoutMs)
{
    if (!hx711_.wait_ready_timeout(timeoutMs)) return false;
    grams = hx711_.get_units(scans);
    return true;
}

bool Scale::steadyGrams(float &grams, PollHook hook)
{
    float recent[3] = {0, 0, 0};
    uint8_t filled = 0;
    bool steady = false;
    const unsigned long start = millis();
    while (millis() - start < board::STEADY_WAIT_MS)
    {
        if (hook) hook();
        recent[0] = recent[1];
        recent[1] = recent[2];
        recent[2] = hx711_.get_units(board::CHECK_SCANS);
        if (filled < 3) filled++;
        const float lo = min(recent[0], min(recent[1], recent[2]));
        const float hi = max(recent[0], max(recent[1], recent[2]));
        if (filled >= 3 && hi - lo <= board::STEADY_BAND_G)
        {
            steady = true;
            break;
        }
    }
    grams = hx711_.get_units(board::AVERAGE_SCANS);
    return steady;
}

long Scale::noiseSpread(uint8_t readings)
{
    long minVal = 2147483647L;
    long maxVal = -2147483647L;
    for (uint8_t i = 0; i < readings; i++)
    {
        const long v = hx711_.read();
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
    }
    return maxVal - minVal;
}

void Scale::startCalibration()
{
    hx711_.set_scale();  // factor of 1 so the readings are raw
    hx711_.tare(100);
}

Scale::CalibrationResult Scale::finishCalibration(float knownGrams)
{
    delay(4000);
    float m[3];
    for (int i = 0; i < 3; i++)
    {
        m[i] = hx711_.get_value(100);
        delay(500);
    }
    const float avg = (m[0] + m[1] + m[2]) / 3.0;
    const float lo = min(m[0], min(m[1], m[2]));
    const float hi = max(m[0], max(m[1], m[2]));
    const float factor = avg / knownGrams;
    if (fabs(factor) < 0.5)
    {
        cancelCalibration();
        return CALIBRATION_NO_CHANGE;
    }
    const bool unsteady = fabs((hi - lo) / avg) * 100.0 > 1.0;

    factor_ = factor;
    calibrated_ = true;
    hx711_.set_scale(factor_);
    EEPROM.update(board::EEPROM_ADDR_MARKER, board::EEPROM_MARKER);
    EEPROM.put(board::EEPROM_ADDR_FACTOR, factor_);
    return unsteady ? CALIBRATION_SAVED_UNSTEADY : CALIBRATION_SAVED;
}
