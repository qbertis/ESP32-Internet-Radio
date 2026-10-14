#ifndef BUTTON_MANAGER_H
#define BUTTON_MANAGER_H

#include <Arduino.h>
#include "SettingsManager.h"
#include "AudioPlayer.h"
#include "DisplayManager.h"
#include "RadioStream.h"

#define BUTTON_NEXT     13
#define BUTTON_PREV     12
#define BUTTON_VOL_UP   14
#define BUTTON_VOL_DN   27
#define BUTTON_EQ_MODE  25
#define BUTTON_MUTE     26

class ButtonManager {
private:
    inline static bool eqModeActive = false;
    inline static int eqParamIndex = 0;
    inline static unsigned long eqButtonPressStart = 0;
    inline static bool eqButtonWasPressed = false;

    inline static bool isMuted = false;
    inline static int volumeBeforeMute = 70;

    inline static unsigned long lastEQChange = 0;

    static constexpr unsigned long LONG_PRESS_TIME = 1000;
    static constexpr unsigned long EQ_CHANGE_DELAY = 150;
    static constexpr unsigned long STATION_CHANGE_DELAY = 500;
    static constexpr unsigned long MUTE_DELAY = 300;
    static constexpr unsigned long VOLUME_REPEAT_DELAY = 120;

public:
    static void initialize() {
        pinMode(BUTTON_NEXT, INPUT_PULLUP);
        pinMode(BUTTON_PREV, INPUT_PULLUP);
        pinMode(BUTTON_VOL_UP, INPUT_PULLUP);
        pinMode(BUTTON_VOL_DN, INPUT_PULLUP);
        pinMode(BUTTON_EQ_MODE, INPUT_PULLUP);
        pinMode(BUTTON_MUTE, INPUT_PULLUP);

        eqModeActive = false;
        eqParamIndex = 0;
        eqButtonPressStart = 0;
        eqButtonWasPressed = false;
        isMuted = false;
        volumeBeforeMute = 70;
        lastEQChange = 0;

        Serial.println("Buttons initialized (Simple EQ + MUTE)");
    }

    static void handleButtons() {
        // Будимо дисплей при натисканні будь-якої кнопки
        if (digitalRead(BUTTON_NEXT)    == LOW ||
            digitalRead(BUTTON_PREV)    == LOW ||
            digitalRead(BUTTON_VOL_UP)  == LOW ||
            digitalRead(BUTTON_VOL_DN)  == LOW ||
            digitalRead(BUTTON_EQ_MODE) == LOW ||
            digitalRead(BUTTON_MUTE)    == LOW) {
            DisplayManager::notifyActivity();
        }
        
        // Синхронізація стану EQ режиму з DisplayManager
        bool currentEQMode = DisplayManager::isEQModeActive();
        if (eqModeActive != currentEQMode) {
            eqModeActive = currentEQMode;
            if (!eqModeActive) {
                eqParamIndex = 0;
                Serial.println("EQ Mode deactivated by display timeout");
            }
        }

        // ===== NEXT / PREV =====
        static bool lastNext = HIGH;
        static bool lastPrev = HIGH;
        static unsigned long lastStationChange = 0;

        bool nextState = digitalRead(BUTTON_NEXT);
        bool prevState = digitalRead(BUTTON_PREV);

        if (lastNext == HIGH && nextState == LOW && !eqModeActive) {
            if (millis() - lastStationChange > STATION_CHANGE_DELAY) {
                lastStationChange = millis();
                Serial.println("NEXT station");
                SettingsManager::nextStation();
                RadioStream::connectToHost();
            }
        }

        if (lastPrev == HIGH && prevState == LOW && !eqModeActive) {
            if (millis() - lastStationChange > STATION_CHANGE_DELAY) {
                lastStationChange = millis();
                Serial.println("PREV station");
                SettingsManager::previousStation();
                RadioStream::connectToHost();
            }
        }

        lastNext = nextState;
        lastPrev = prevState;

        // ===== MUTE =====
        static bool lastMute = HIGH;
        static unsigned long lastMuteChange = 0;

        bool muteState = digitalRead(BUTTON_MUTE);

        if (lastMute == HIGH && muteState == LOW && !eqModeActive) {
            if (millis() - lastMuteChange > MUTE_DELAY) {
                lastMuteChange = millis();
                toggleMute();
            }
        }

        lastMute = muteState;

        // ===== EQ MODE =====
        static bool lastEq = HIGH;
        bool eqState = digitalRead(BUTTON_EQ_MODE);

        if (lastEq == HIGH && eqState == LOW) {
            eqButtonPressStart = millis();
            eqButtonWasPressed = true;
            Serial.println("EQ button pressed");
        }

        if (lastEq == LOW && eqState == HIGH && eqButtonWasPressed) {
            unsigned long pressDuration = millis() - eqButtonPressStart;

            if (pressDuration >= LONG_PRESS_TIME) {
                if (eqModeActive) {
                    exitEQMode();
                }
            } else {
                if (eqModeActive) {
                    switchEQParam();
                } else {
                    enterEQMode();
                }
            }

            eqButtonWasPressed = false;
        }

        lastEq = eqState;

        // ===== VOLUME / EQ ADJUST =====
        handleVolumeButtons();
    }

    static void toggleMute() {
        if (isMuted) {
            SettingsManager::setVolume(volumeBeforeMute);
            AudioPlayer::setVolume(volumeBeforeMute);
            isMuted = false;
            DisplayManager::showMessage("Unmuted");
            Serial.println("MUTE OFF");
        } else {
            volumeBeforeMute = SettingsManager::getVolume();
            SettingsManager::setVolume(0);
            AudioPlayer::setVolume(0);
            isMuted = true;
            DisplayManager::showMessage("Muted");
            Serial.println("MUTE ON");
        }
    }

    static void enterEQMode() {
        eqModeActive = true;
        eqParamIndex = 0;
        Serial.println("EQ Mode entered");
        DisplayManager::showEQMode(true, eqParamIndex);
    }

    static void exitEQMode() {
        eqModeActive = false;
        eqParamIndex = 0;
        Serial.println("EQ Mode exited by button");
        DisplayManager::showEQMode(false, eqParamIndex);
        DisplayManager::showMessage("EQ Exit");
    }

    static void switchEQParam() {
        eqParamIndex = (eqParamIndex + 1) % 2;

        const char* paramNames[] = {"Bass Gain", "Treble Gain"};
        Serial.printf("EQ Param switched to: %s (%d)\n", paramNames[eqParamIndex], eqParamIndex);

        DisplayManager::showEQMode(true, eqParamIndex);
    }

    static void adjustEQValue(int delta) {
        if (!eqModeActive) return;

        // Прибираємо стару затримку EQ_CHANGE_DELAY, 
        // бо тепер швидкість контролює VOLUME_REPEAT_DELAY у handleVolumeButtons

        switch (eqParamIndex) {
            case 0: { // Bass Gain (0-15 dB)
                int current = SettingsManager::getBass();
                int val = constrain(current + delta, BASS_AMP_MIN, BASS_AMP_MAX);

                if (val != current) {
                    SettingsManager::setBass(val);
                    AudioPlayer::updateEQ();
                    Serial.printf("Bass Gain: +%d dB\n", val);
                    DisplayManager::showEQValue(eqParamIndex, val);
                }
                break;
            }

            case 1: { // Treble Gain (-8..7, крок 1.5 dB)
                int current = SettingsManager::getTreble();
                int val = constrain(current + delta, TREBLE_AMP_MIN, TREBLE_AMP_MAX);

                if (val != current) {
                    SettingsManager::setTreble(val);
                    AudioPlayer::updateEQ();
                    Serial.printf("Treble Gain: %.1f dB\n", val * 1.5f);
                    DisplayManager::showEQValue(eqParamIndex, val);
                }
                break;
            }
        }
    }

    static bool isEQModeActive() {
        return eqModeActive;
    }

private:
    static void handleVolumeButtons() {
        static unsigned long lastChange = 0;
        static unsigned long pressStartTimeUp = 0;
        static unsigned long pressStartTimeDn = 0;
        static bool lastUp = false;
        static bool lastDn = false;

        const unsigned long HOLD_DELAY = 400; // затримка перед початком автоповтору

        bool up = (digitalRead(BUTTON_VOL_UP) == LOW);
        bool dn = (digitalRead(BUTTON_VOL_DN) == LOW);

        // --- ЛОГІКА UP ---
        if (up) {
            if (!lastUp) pressStartTimeUp = millis(); // Тільки натиснули

            unsigned long duration = millis() - pressStartTimeUp;
            // Спрацьовує якщо: тільки натиснули АБО утримуємо довше HOLD_DELAY з інтервалом VOLUME_REPEAT_DELAY
            if (!lastUp || (duration > HOLD_DELAY && (millis() - lastChange >= VOLUME_REPEAT_DELAY))) {
                if (eqModeActive) {
                    adjustEQValue(1);
                } else {
                    int v = SettingsManager::getVolume();
                    if (v < 100) {
                        SettingsManager::setVolume(v + 1);
                        AudioPlayer::setVolume(SettingsManager::getVolume());
                        if (isMuted) { isMuted = false; DisplayManager::showMessage("Unmuted"); }
                    }
                }
                lastChange = millis();
            }
        }

        // --- ЛОГІКА DN ---
        if (dn) {
            if (!lastDn) pressStartTimeDn = millis(); // Тільки натиснули

            unsigned long duration = millis() - pressStartTimeDn;
            if (!lastDn || (duration > HOLD_DELAY && (millis() - lastChange >= VOLUME_REPEAT_DELAY))) {
                if (eqModeActive) {
                    adjustEQValue(-1);
                } else {
                    int v = SettingsManager::getVolume();
                    if (v > 0) {
                        SettingsManager::setVolume(v - 1);
                        AudioPlayer::setVolume(SettingsManager::getVolume());
                        if (isMuted && (v - 1) > 0) { isMuted = false; DisplayManager::showMessage("Unmuted"); }
                    }
                }
                lastChange = millis();
            }
        }

        lastUp = up;
        lastDn = dn;
    }
};    

#endif