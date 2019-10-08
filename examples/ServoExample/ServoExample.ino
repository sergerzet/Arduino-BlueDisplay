/*
 *  ServoExample.cpp
 *  Demo of using the BlueDisplay library for HC-05 on Arduino
 *  The accelerometer sensor of the android display is used to control two servos
 *  in a frame which holds a laser.
 *  This is an example for using a fullscreen GUI.
 *
 *  If no BD connection available, the servo first marks the border and then moves randomly in this area (Cat Mover).
 *
 *  Zero -> the actual sensor position is taken as the servos 90/90 degree position.
 *  Bias (reverse of Zero) -> take actual servos position as position for horizontal sensors position.
 *  Move -> moves randomly in the programmed border. Currently horizontal 45 to 135 and vertical 0 to 45
 *
 *
 *  Copyright (C) 2015  Armin Joachimsmeyer
 *  armin.joachimsmeyer@gmail.com
 *
 *  This file is part of BlueDisplay.
 *  BlueDisplay is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/gpl.html>.
 *
 */

#include <Arduino.h>

#include "ServoEasing.h" // for smooth auto moving

#include <BlueDisplay.h>

#define VERSION_EXAMPLE "1.1"

// Change this if you have programmed the HC-05 module for another baud rate
#ifndef HC_05_BAUD_RATE
//#define HC_05_BAUD_RATE BAUD_115200
#define HC_05_BAUD_RATE BAUD_9600
#endif

const int LASER_POWER_PIN = 5; // uses timer0

// These pins are used by Timer 2 and can be used without overhead by using SimpleServo functions of ArduinoUtils.cpp - not used yet!
const int HORIZONTAL_SERVO_PIN = 10;
const int VERTICAL_SERVO_PIN = 9;

struct ServoControlStruct {
    uint16_t minDegree;
    uint16_t maxDegree;
};
ServoControlStruct ServoHorizontalControl;
ServoControlStruct ServoVerticalControl;

ServoEasing ServoHorizontal;
ServoEasing ServoVertical;

/*
 * Buttons
 */
BDButton TouchButtonServosStartStop;
void doServosStartStop(BDButton * aTheTochedButton, int16_t aValue);
void resetOutputs(void);
bool sStarted;

BDButton TouchButtonSetZero;
void doSetZero(BDButton * aTheTouchedButton, int16_t aValue);
#define CALLS_FOR_ZERO_ADJUSTMENT 8
int tSensorChangeCallCount;
float sXZeroCompensationValue; // This is the value of the sensor which is taken as "Zero" (90 servo) position
float sYZeroCompensationValue;

BDButton TouchButtonSetBias;
void doSetBias(BDButton * aTheTouchedButton, int16_t aValue);
float sXBiasValue = 0;
float sYBiasValue = 0;
float sLastSensorXValue;
float sLastSensorYValue;

BDButton TouchButtonAutoMove;
void doEnableAutoMove(BDButton * aTheTouchedButton, int16_t aValue);
bool sDoAutomove;

/*
 * Slider
 */
#define SLIDER_BACKGROUND_COLOR COLOR_YELLOW
#define SLIDER_BAR_COLOR COLOR_GREEN
#define SLIDER_THRESHOLD_COLOR COLOR_BLUE

/*
 * Laser
 */
BDSlider SliderLaserPower;
// storage for laser analogWrite and laser slider value for next start if we stop laser
uint8_t sLaserPowerValue;
uint16_t sLastLaserSliderValue;
void doLaserPowerSlider(BDSlider * aTheTouchedSlider, uint16_t aValue);

/*
 * Vertical
 */
BDSlider SliderUp;
BDSlider SliderDown;
int sLastSliderVerticalValue = 0;
int sLastVerticalValue = 0;

/*
 * Horizontal
 */
BDSlider SliderRight;
BDSlider SliderLeft;
int sLastLeftRightValue = 0;

/*
 * Timing
 */
uint32_t sMillisOfLastReveivedEvent = 0;
#define SENSOR_RECEIVE_TIMEOUT_MILLIS 500
uint32_t sMillisOfLastVCCInfo = 0;
#define VCC_INFO_PERIOD_MILLIS 1000

/*
 * Layout
 */
int sActualDisplayWidth;
int sActualDisplayHeight;
int sSliderSize;
#define VALUE_X_SLIDER_DEAD_BAND (sSliderSize/2)
int sSliderHeight;
int sSliderWidth;
#define SLIDER_LEFT_RIGHT_THRESHOLD (sSliderWidth/4)
int sTextSize;
int sTextSizeVCC;

// a string buffer for any purpose...
char sStringBuffer[128];

void doSensorChange(uint8_t aSensorType, struct SensorCallback * aSensorCallbackInfo);

/*******************************************************************************************
 * Program code starts here
 *******************************************************************************************/

void drawGui(void) {
    BlueDisplay1.clearDisplay();
    SliderUp.drawSlider();
    SliderDown.drawSlider();
    SliderRight.drawSlider();
    SliderLeft.drawSlider();
    SliderLaserPower.drawSlider();

    TouchButtonSetZero.drawButton();
    TouchButtonSetBias.drawButton();
    TouchButtonAutoMove.drawButton();
    TouchButtonServosStartStop.drawButton();
}

void initDisplay(void) {
    /*
     * handle display size
     */
    sActualDisplayWidth = BlueDisplay1.getMaxDisplayWidth();
    sActualDisplayHeight = BlueDisplay1.getMaxDisplayHeight();
    if (sActualDisplayWidth < sActualDisplayHeight) {
        // Portrait -> change to landscape 3/2 format
        sActualDisplayHeight = (sActualDisplayWidth / 3) * 2;
    }
    /*
     * compute layout values
     */
    sSliderSize = sActualDisplayWidth / 32;
    // 3/8 of sActualDisplayHeight
    sSliderHeight = (sActualDisplayHeight / 4) + (sActualDisplayHeight / 8);
    sSliderWidth = sSliderHeight - VALUE_X_SLIDER_DEAD_BAND;

    sTextSize = sActualDisplayHeight / 16;
    sTextSizeVCC = sTextSize * 2;

    BlueDisplay1.setFlagsAndSize(BD_FLAG_FIRST_RESET_ALL | BD_FLAG_TOUCH_BASIC_DISABLE, sActualDisplayWidth, sActualDisplayHeight);

    tSensorChangeCallCount = 0;
    // Since landscape has 2 orientations, let the user choose the right one.
    BlueDisplay1.setScreenOrientationLock(FLAG_SCREEN_ORIENTATION_LOCK_ACTUAL);

    uint16_t tSliderSize = sActualDisplayHeight / 2;
    SliderLaserPower.init(0, sActualDisplayHeight / 8, sSliderSize * 4, tSliderSize, (tSliderSize * 2) / 3, tSliderSize / 2,
    SLIDER_BACKGROUND_COLOR, SLIDER_BAR_COLOR, FLAG_SLIDER_VERTICAL_SHOW_NOTHING, &doLaserPowerSlider);
    SliderLaserPower.setCaptionProperties(sTextSize, FLAG_SLIDER_CAPTION_ALIGN_LEFT_BELOW, 4, COLOR_RED, COLOR_WHITE);
    SliderLaserPower.setCaption("Laser");

    /*
     * 4 Slider
     */
// Position Slider at middle of screen
    SliderUp.init((sActualDisplayWidth - sSliderSize) / 2, (sActualDisplayHeight / 2) - sSliderHeight + sSliderSize, sSliderSize,
            sSliderHeight, sSliderHeight, 0, SLIDER_BACKGROUND_COLOR, SLIDER_BAR_COLOR, FLAG_SLIDER_IS_ONLY_OUTPUT, NULL);
    SliderUp.setBarThresholdColor(SLIDER_THRESHOLD_COLOR);

    SliderDown.init((sActualDisplayWidth - sSliderSize) / 2, (sActualDisplayHeight / 2) + sSliderSize, sSliderSize,
            -(sSliderHeight), sSliderHeight, 0, SLIDER_BACKGROUND_COLOR, SLIDER_BAR_COLOR, FLAG_SLIDER_IS_ONLY_OUTPUT, NULL);
    SliderDown.setBarThresholdColor(SLIDER_THRESHOLD_COLOR);

// Position slider right from vertical one at middle of screen
    SliderRight.init((sActualDisplayWidth + sSliderSize) / 2, ((sActualDisplayHeight - sSliderSize) / 2) + sSliderSize, sSliderSize,
            sSliderWidth,
            SLIDER_LEFT_RIGHT_THRESHOLD, 0, SLIDER_BACKGROUND_COLOR, SLIDER_BAR_COLOR,
            FLAG_SLIDER_IS_HORIZONTAL | FLAG_SLIDER_IS_ONLY_OUTPUT, NULL);
    SliderRight.setBarThresholdColor( SLIDER_THRESHOLD_COLOR);

// Position inverse slider left from vertical one at middle of screen
    SliderLeft.init(((sActualDisplayWidth - sSliderSize) / 2) - sSliderWidth,
            ((sActualDisplayHeight - sSliderSize) / 2) + sSliderSize, sSliderSize, -(sSliderWidth), SLIDER_LEFT_RIGHT_THRESHOLD, 0,
            SLIDER_BACKGROUND_COLOR, SLIDER_BAR_COLOR, FLAG_SLIDER_IS_HORIZONTAL | FLAG_SLIDER_IS_ONLY_OUTPUT, NULL);
    SliderLeft.setBarThresholdColor(SLIDER_THRESHOLD_COLOR);

    /*
     * Buttons
     */
    TouchButtonServosStartStop.init(0, sActualDisplayHeight - sActualDisplayHeight / 4, sActualDisplayWidth / 4,
            sActualDisplayHeight / 4,
            COLOR_BLUE, "Start", sTextSizeVCC, FLAG_BUTTON_DO_BEEP_ON_TOUCH | FLAG_BUTTON_TYPE_TOGGLE_RED_GREEN, sStarted,
            &doServosStartStop);
    TouchButtonServosStartStop.setCaptionPGMForValueTrue(PSTR("Stop"));

    TouchButtonSetBias.init(sActualDisplayWidth - sActualDisplayWidth / 4,
            (sActualDisplayHeight - sActualDisplayHeight / 2) - sActualDisplayHeight / 32, sActualDisplayWidth / 4,
            sActualDisplayHeight / 4, COLOR_RED, "Bias", sTextSizeVCC, FLAG_BUTTON_DO_BEEP_ON_TOUCH, 0, &doSetBias);

    TouchButtonSetZero.init(sActualDisplayWidth - sActualDisplayWidth / 4, sActualDisplayHeight - sActualDisplayHeight / 4,
            sActualDisplayWidth / 4, sActualDisplayHeight / 4, COLOR_RED, "Zero", sTextSizeVCC, FLAG_BUTTON_DO_BEEP_ON_TOUCH, 0,
            &doSetZero);

    TouchButtonAutoMove.init(sActualDisplayWidth - sActualDisplayWidth / 4, sActualDisplayHeight / 4 - sActualDisplayHeight / 16,
            sActualDisplayWidth / 4, sActualDisplayHeight / 4, COLOR_BLUE, "Move", sTextSizeVCC,
            FLAG_BUTTON_DO_BEEP_ON_TOUCH | FLAG_BUTTON_TYPE_TOGGLE_RED_GREEN, sDoAutomove, &doEnableAutoMove);
    TouchButtonAutoMove.setCaptionPGMForValueTrue(PSTR("Stop"));

}

void setup() {
// initialize the digital pin as an output.
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LASER_POWER_PIN, OUTPUT);
    analogWrite(LASER_POWER_PIN, 0);

#ifndef USE_STANDARD_SERIAL
    /*
     * If you want to see serial output if not connected with BlueDisplay comment line 39 in BlueSerial.h or use global #define USE_STANDARD_SERIAL
     * e.g. with -DUSE_STANDARD_SERIAL as compiler parameter for c++ in order to force the BlueDisplay library to use the Arduino Serial object
     * and release the SimpleSerial interrupt handler '__vector_18'
     */
    initSimpleSerial(HC_05_BAUD_RATE);
#else
    Serial.begin(HC_05_BAUD_RATE);
#endif

    // Register callback handler and check for connection
    BlueDisplay1.initCommunication(&initDisplay, &drawGui);

    if (!BlueDisplay1.mConnectionEstablished) {
#ifdef USE_STANDARD_SERIAL
        while (!Serial)
            ; //delay for Leonardo
        // Just to know which program is running on my Arduino
        Serial.println(F("START " __FILE__ "\r\nVersion " VERSION_EXAMPLE " from  " __DATE__));
#endif
        sStarted = true;
    } else {
        // set sLastLaserSliderValue and sLaserPowerValue and enables laser
        doLaserPowerSlider(NULL, sActualDisplayHeight / 8);
        doServosStartStop(NULL, true);
    }

    /*
     * Set up servos
     */
    ServoHorizontal.attach(HORIZONTAL_SERVO_PIN);
    ServoVertical.attach(VERTICAL_SERVO_PIN);
    ServoHorizontal.setEasingType(EASE_QUADRATIC_IN_OUT);
    ServoVertical.setEasingType(EASE_QUADRATIC_IN_OUT);

    ServoHorizontalControl.minDegree = 45;
    ServoHorizontalControl.maxDegree = 135;
    ServoVerticalControl.minDegree = 0;
    ServoVerticalControl.maxDegree = 45;

    resetOutputs();

    delay(4000);

    if (!BlueDisplay1.isConnectionEstablished()) {
        sDoAutomove = true;
        /*
         * show border of area which can be reached by laser
         */
#ifdef USE_STANDARD_SERIAL
        Serial.println(F("Not connected to BlueDisplay -> mark border of area and then do auto move."));
#endif
        /*
         * show border of area which can be reached by laser
         */
        ServoHorizontal.write(ServoHorizontalControl.minDegree);
        ServoVertical.write(ServoVerticalControl.minDegree);
        delay(500);
        analogWrite(LASER_POWER_PIN, 255);
        ServoHorizontal.easeTo(ServoHorizontalControl.maxDegree, 50);
        ServoVertical.easeTo(ServoVerticalControl.maxDegree, 50);
        ServoHorizontal.easeTo(ServoHorizontalControl.minDegree, 50);
        ServoVertical.easeTo(ServoVerticalControl.minDegree, 50);
    }
}

uint8_t getRandomValue(ServoControlStruct * aServoControlStruct, ServoEasing * aServoEasing) {
    /*
     * get new different value
     */
    uint8_t tNewTargetAngle;
    do {
        tNewTargetAngle = random(aServoControlStruct->minDegree, aServoControlStruct->maxDegree);
    } while (tNewTargetAngle == aServoEasing->MicrosecondsOrUnitsToDegree(aServoEasing->mCurrentMicrosecondsOrUnits)); // do not accept current angle as new value
    return tNewTargetAngle;
}

void loop() {

    uint32_t tMillis = millis();

    if (sDoAutomove) {
        /*
         * Start random auto-moves, if enabled or no Bluetooth connection available
         */
        if (!ServoHorizontal.isMoving()) {
            // start new move
            delay(random(500));
            uint8_t tNewHorizontal = getRandomValue(&ServoHorizontalControl, &ServoHorizontal);
            uint8_t tNewVertical = getRandomValue(&ServoVerticalControl, &ServoVertical);
            int tSpeed = random(10, 90);
            if (!BlueDisplay1.isConnectionEstablished()) {
#ifdef USE_STANDARD_SERIAL
                Serial.print(F("Move to H="));
                Serial.print(tNewHorizontal);
                Serial.print(F(" V="));
                Serial.print(tNewVertical);
                Serial.print(F(" S="));
                Serial.println(tSpeed);
#endif
            }
            ServoHorizontal.setEaseTo(tNewHorizontal, tSpeed);
            ServoVertical.setEaseTo(tNewVertical, tSpeed);
            synchronizeAllServosAndStartInterrupt();
        }
    } else {
        /*
         * Stop servo output if stop requested or if connection lost
         */
        if (sStarted && (tMillis - sMillisOfLastReveivedEvent) > SENSOR_RECEIVE_TIMEOUT_MILLIS) {
            disableServoEasingInterrupt();
            resetOutputs();
        }
    }

    if (BlueDisplay1.isConnectionEstablished()) {
        /*
         * Print VCC and temperature each second
         */
        BlueDisplay1.printVCCAndTemperaturePeriodically(sActualDisplayWidth / 4, sTextSizeVCC, sTextSizeVCC,
        VCC_INFO_PERIOD_MILLIS);
    }

    /*
     * Check if receive buffer contains an event
     */
    checkAndHandleEvents();
}

#pragma GCC diagnostic ignored "-Wunused-parameter"

/*
 * Use logarithmic scale: 0 -> 0, (sActualDisplayHeight / 2) -> 255
 */
void doLaserPowerSlider(BDSlider * aTheTouchedSlider, uint16_t aValue) {
    sLastLaserSliderValue = aValue;
    float tValue = aValue;
    tValue = (tValue * 5) / sActualDisplayHeight; // gives 0-2.5 for 0 - sActualDisplayHeight/2
// 950 byte program space needed for pow() and log10f()
    tValue = pow(10, tValue);
    if (tValue > 255) {
        tValue = 255;
    }
    sLaserPowerValue = tValue;
//    BlueDisplay1.debug("Laser=", sLaserPowerValue);
    analogWrite(LASER_POWER_PIN, sLaserPowerValue);
}

/*
 * Handle Start/Stop
 */
void doServosStartStop(BDButton * aTheTouchedButton, int16_t aValue) {
    sStarted = aValue;
    if (sStarted) {
        /*
         * Do start
         */
        if (sDoAutomove) {
            // first stop auto moves
            sDoAutomove = false;
            TouchButtonAutoMove.setValueAndDraw(false);
        }
        registerSensorChangeCallback(FLAG_SENSOR_TYPE_ACCELEROMETER, FLAG_SENSOR_DELAY_UI, FLAG_SENSOR_NO_FILTER, &doSensorChange);
        sMillisOfLastReveivedEvent = millis();
        analogWrite(LASER_POWER_PIN, sLaserPowerValue);
        SliderLaserPower.setActualValueAndDrawBar(sLastLaserSliderValue);
    } else {
        /*
         * Do stop
         */
        registerSensorChangeCallback(FLAG_SENSOR_TYPE_ACCELEROMETER, FLAG_SENSOR_DELAY_UI, FLAG_SENSOR_NO_FILTER, NULL);
        if (!sDoAutomove) {
            resetOutputs();
        }
    }
}

/*
 * Stop output signals
 */
void resetOutputs(void) {
    ServoHorizontal.write(90);
    ServoVertical.write(90);
    analogWrite(LASER_POWER_PIN, 0);
    if (BlueDisplay1.mConnectionEstablished) {
        SliderLaserPower.setActualValueAndDrawBar(0);
    }
}

/*
 * The actual sensor position is taken as the 90/90 degree position
 */
void doSetZero(BDButton * aTheTouchedButton, int16_t aValue) {
// wait for end of touch vibration
    delay(10);
    tSensorChangeCallCount = 0;
}

void doEnableAutoMove(BDButton * aTheTouchedButton, int16_t aValue) {
    sDoAutomove = aValue;
    if (sDoAutomove) {
        if (sStarted) {
            // first stop sensor moves
            doServosStartStop(NULL, false);
            TouchButtonServosStartStop.setValueAndDraw(false);
        }
        ServoHorizontal.setEaseTo(90, 10);
        ServoVertical.startEaseTo(90, 10);
    }
}

/*
 * take actual position as horizontal one
 */
void doSetBias(BDButton * aTheTouchedButton, int16_t aValue) {
// wait for end of touch vibration
    delay(10);
    // this will do the trick
    sXBiasValue = sLastSensorXValue;
    sYBiasValue = sLastSensorYValue;
    // show message in order to see the effect
    BlueDisplay1.clearDisplay();
    BlueDisplay1.drawText(0, sTextSize + getTextAscend(sTextSize * 3), "old position is taken \rfor horizontal input\r",
            sTextSize * 2,
            COLOR_BLACK, COLOR_GREEN);
    delayMillisWithCheckAndHandleEvents(2500);
    drawGui();
}

/*
 * Values are between +10 at 90 degree (canvas top is up) and -10 (canvas bottom is up)
 */
void processVerticalSensorValue(float aSensorValue) {

    int tVerticalServoValue = (aSensorValue + sYBiasValue - sYZeroCompensationValue) * 12;
// overflow handling
    if (tVerticalServoValue > 90) {
        tVerticalServoValue = 90;
    } else if (tVerticalServoValue < -90) {
        tVerticalServoValue = -90;
    }
    ServoVertical.write(tVerticalServoValue + 90);

// Active slider detection
    BDSlider tActiveSlider;
    BDSlider tInactiveSlider;
    if (tVerticalServoValue >= 0) {
        tActiveSlider = SliderUp;
        tInactiveSlider = SliderDown;
    } else {
        tActiveSlider = SliderDown;
        tInactiveSlider = SliderUp;
        tVerticalServoValue = -tVerticalServoValue;
    }

// use this as delay between deactivating one channel and activating the other
    int tSliderValue = (tVerticalServoValue * sSliderHeight) / 90;
    if (sLastSliderVerticalValue != tSliderValue) {
        sLastSliderVerticalValue = tSliderValue;
        tActiveSlider.setActualValueAndDrawBar(tSliderValue);
        tInactiveSlider.setActualValueAndDrawBar(0);
        if (sLastVerticalValue != tVerticalServoValue) {
            sLastVerticalValue = tVerticalServoValue;
            sprintf(sStringBuffer, "%3d", tVerticalServoValue);
            SliderDown.printValue(sStringBuffer);
        }
    }
}

/*
 * Values are between +10 at 90 degree (canvas right is up) and -10 (canvas left is up)
 */
void processHorizontalSensorValue(float aSensorValue) {

    int tHorizontalValue = (aSensorValue + sXBiasValue - sXZeroCompensationValue) * 18;
// overflow handling
    if (tHorizontalValue > 90) {
        tHorizontalValue = 90;
    } else if (tHorizontalValue < -90) {
        tHorizontalValue = -90;
    }
    ServoHorizontal.write(tHorizontalValue + 90);

// left right handling
    BDSlider tActiveSlider;
    BDSlider tInactiveSlider;
    if (tHorizontalValue >= 0) {
        tActiveSlider = SliderLeft;
        tInactiveSlider = SliderRight;
    } else {
        tActiveSlider = SliderRight;
        tInactiveSlider = SliderLeft;
        tHorizontalValue = -tHorizontalValue;
    }

// scale value for full scale =SLIDER_WIDTH at at 30 degree
    tHorizontalValue = (tHorizontalValue * sSliderHeight) / 90;

// dead band handling for slider
    if (tHorizontalValue > VALUE_X_SLIDER_DEAD_BAND) {
        tHorizontalValue = tHorizontalValue - VALUE_X_SLIDER_DEAD_BAND;
    } else {
        tHorizontalValue = 0;
    }

// use this as delay between deactivating one pin and activating the other
    if (sLastLeftRightValue != tHorizontalValue) {
        sLastLeftRightValue = tHorizontalValue;
        tActiveSlider.setActualValueAndDrawBar(tHorizontalValue);
        tInactiveSlider.setActualValueAndDrawBar(0);
    }
}

/*
 * Not used yet
 */
void printSensorInfo(struct SensorCallback* aSensorCallbackInfo) {
    dtostrf(aSensorCallbackInfo->ValueX, 7, 4, &sStringBuffer[50]);
    dtostrf(aSensorCallbackInfo->ValueY, 7, 4, &sStringBuffer[60]);
    dtostrf(aSensorCallbackInfo->ValueZ, 7, 4, &sStringBuffer[70]);
    dtostrf(sYZeroCompensationValue, 7, 4, &sStringBuffer[80]);
    snprintf(sStringBuffer, sizeof sStringBuffer, "X=%s Y=%s Z=%s Zero=%s", &sStringBuffer[50], &sStringBuffer[60],
            &sStringBuffer[70], &sStringBuffer[80]);
    BlueDisplay1.drawText(0, sTextSize, sStringBuffer, sTextSize, COLOR_BLACK, COLOR_GREEN);
}

/*
 * Sensor callback handler
 */
void doSensorChange(uint8_t aSensorType, struct SensorCallback * aSensorCallbackInfo) {
    static float sXZeroValueAdded;
    static float sYZeroValueAdded;
    if (tSensorChangeCallCount < CALLS_FOR_ZERO_ADJUSTMENT) {
        if (tSensorChangeCallCount == 0) {
            // init values
            sXZeroValueAdded = 0;
            sYZeroValueAdded = 0;
        }
        // Add for zero adjustment
        sXZeroValueAdded += aSensorCallbackInfo->ValueX;
        sYZeroValueAdded += aSensorCallbackInfo->ValueY;
    } else if (tSensorChangeCallCount == CALLS_FOR_ZERO_ADJUSTMENT) {
        // compute and set zero value
        sXZeroCompensationValue = sXZeroValueAdded / CALLS_FOR_ZERO_ADJUSTMENT;
        sYZeroCompensationValue = sYZeroValueAdded / CALLS_FOR_ZERO_ADJUSTMENT;
        sXBiasValue = 0;
        sYBiasValue = 0;
        BlueDisplay1.playTone(24);
    } else {
        /*
         * regular operation here!
         */
        tSensorChangeCallCount = CALLS_FOR_ZERO_ADJUSTMENT + 1; // to prevent overflow
//        printSensorInfo(aSensorCallbackInfo);
        if (sStarted) {
            processVerticalSensorValue(aSensorCallbackInfo->ValueY);
            processHorizontalSensorValue(aSensorCallbackInfo->ValueX);
            sLastSensorXValue = aSensorCallbackInfo->ValueX;
            sLastSensorYValue = aSensorCallbackInfo->ValueY;
        }
    }
    sMillisOfLastReveivedEvent = millis();
    tSensorChangeCallCount++;
}
