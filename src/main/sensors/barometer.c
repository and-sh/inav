/*
 * This file is part of Cleanflight.
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "platform.h"

#include "common/maths.h"

#include "drivers/barometer.h"

#include "sensors/barometer.h"

#include "flight/hil.h"

baro_t baro;                        // barometer access functions

#ifdef BARO

static uint16_t calibratingB = 0;      // baro calibration = get new ground pressure value
static int32_t baroPressure = 0;
static int32_t baroGroundAltitude = 0;
static int32_t baroGroundPressure = 0;

static barometerConfig_t *barometerConfig;

void useBarometerConfig(barometerConfig_t *barometerConfigToUse)
{
    barometerConfig = barometerConfigToUse;
}

bool isBaroCalibrationComplete(void)
{
    return calibratingB == 0;
}

void baroSetCalibrationCycles(uint16_t calibrationCyclesRequired)
{
    calibratingB = calibrationCyclesRequired;
}

static bool baroReady = false;

#define PRESSURE_SAMPLES_MEDIAN 3

static int32_t applyBarometerMedianFilter(int32_t newPressureReading)
{
    static int32_t barometerFilterSamples[PRESSURE_SAMPLES_MEDIAN];
    static int currentFilterSampleIndex = 0;
    static bool medianFilterReady = false;
    int nextSampleIndex;

    nextSampleIndex = (currentFilterSampleIndex + 1);
    if (nextSampleIndex == PRESSURE_SAMPLES_MEDIAN) {
        nextSampleIndex = 0;
        medianFilterReady = true;
    }

    barometerFilterSamples[currentFilterSampleIndex] = newPressureReading;
    currentFilterSampleIndex = nextSampleIndex;

    if (medianFilterReady)
        return quickMedianFilter3(barometerFilterSamples);
    else
        return newPressureReading;
}

typedef enum {
    BAROMETER_NEEDS_SAMPLES = 0,
    BAROMETER_NEEDS_CALCULATION
} barometerState_e;

bool isBaroReady(void)
{
    return baroReady;
}

uint32_t baroUpdate(void)
{
    static barometerState_e state = BAROMETER_NEEDS_SAMPLES;

    switch (state) {
        default:
        case BAROMETER_NEEDS_SAMPLES:
            baro.dev.get_ut();
            baro.dev.start_up();
            state = BAROMETER_NEEDS_CALCULATION;
            return baro.dev.up_delay;
        break;

        case BAROMETER_NEEDS_CALCULATION:
            baro.dev.get_up();
            baro.dev.start_ut();
            baro.dev.calculate(&baroPressure, &baro.baroTemperature);
            if (barometerConfig->use_median_filtering) {
                baroPressure = applyBarometerMedianFilter(baroPressure);
            }
            state = BAROMETER_NEEDS_SAMPLES;
            return baro.dev.ut_delay;
        break;
    }
}

static void performBaroCalibrationCycle(void)
{
    baroGroundPressure -= baroGroundPressure / 8;
    baroGroundPressure += baroPressure;
    baroGroundAltitude = (1.0f - powf((baroGroundPressure / 8) / 101325.0f, 0.190295f)) * 4433000.0f;

    calibratingB--;
}

int32_t baroCalculateAltitude(void)
{
    if (!isBaroCalibrationComplete()) {
        performBaroCalibrationCycle();
        baro.BaroAlt = 0;
    }
    else {
#ifdef HIL
        if (hilActive) {
            baro.BaroAlt = hilToFC.baroAlt;
            return baro.BaroAlt;
        }
#endif
        // calculates height from ground via baro readings
        // see: https://github.com/diydrones/ardupilot/blob/master/libraries/AP_Baro/AP_Baro.cpp#L140
        baro.BaroAlt = lrintf((1.0f - powf((float)(baroPressure) / 101325.0f, 0.190295f)) * 4433000.0f); // in cm
        baro.BaroAlt -= baroGroundAltitude;
    }

    return baro.BaroAlt;
}

#endif /* BARO */
