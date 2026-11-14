#pragma once

#include <board.h>
#include <HX711.h>

// HX711 load cell with its calibration factor stored in EEPROM on this board.
class Scale
{
public:
    // Loads the saved factor (if any) and tares. Returns false if the HX711 does not answer.
    bool begin();

    bool ready(unsigned long timeoutMs = board::SCALE_READY_TIMEOUT_MS);
    bool calibrated() const { return calibrated_; }
    float factor() const { return factor_; }

    void tare(uint8_t scans = board::AVERAGE_SCANS) { hx711_.tare(scans); }
    float grams(uint8_t scans = board::AVERAGE_SCANS) { return hx711_.get_units(scans); }

    // Waits up to timeoutMs for a quick check to show any weight. Runs hook between checks.
    bool waitForWeight(unsigned long timeoutMs, PollHook hook = nullptr);

    // One reading that never blocks forever: false if the HX711 is not ready within timeoutMs.
    // Use scans = 1 where the HX711 may disappear (each extra scan waits for it without a timeout).
    bool tryGrams(float &grams, uint8_t scans = 1, unsigned long timeoutMs = board::SCALE_READ_TIMEOUT_MS);

    // Waits until 3 quick checks agree within STEADY_BAND_G, then takes a full reading. Runs hook
    // (if set) between checks. Returns true if the reading became steady before STEADY_WAIT_MS.
    bool steadyGrams(float &grams, PollHook hook = nullptr);

    // Raw spread (max - min) over a number of raw readings.
    long noiseSpread(uint8_t readings = 50);

    // Two-phase calibration so the app can prompt the user in between.
    // 1. startCalibration(): raw readings (factor 1) and zeroing, about 10 s.
    // 2. finishCalibration(knownGrams): measures, about 30 s with the settle delay, and saves.
    // cancelCalibration() restores the previous factor.
    enum CalibrationResult : uint8_t
    {
        CALIBRATION_SAVED,
        CALIBRATION_SAVED_UNSTEADY,  // saved, but readings varied more than 1 %
        CALIBRATION_NO_CHANGE,       // nothing on the scale; previous factor kept
    };
    void startCalibration();
    CalibrationResult finishCalibration(float knownGrams);
    void cancelCalibration() { hx711_.set_scale(factor_); }

private:
    HX711 hx711_;
    float factor_ = board::DEFAULT_CALIBRATION_FACTOR;
    bool calibrated_ = false;
};
