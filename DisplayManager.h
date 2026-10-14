#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <U8g2lib.h>
#include "SettingsManager.h"
#include "NTPManager.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

class DisplayManager {
public:
    static bool showMsgActive;
    static unsigned long showMsgUntil;
    static String showMsgText;
    static String showMsgIp;
    
private:
    static unsigned long lastUpdateTime;
    static const unsigned long UPDATE_INTERVAL = 250;
    static int textPosition;
    static bool directionLeft;
    static unsigned long lastScrollUpdate;
    static const unsigned long SCROLL_INTERVAL = 250;
    static bool apModeActive;
    static int lastStationIndex;
    static String cachedTime;
    static String cachedDate;
    static unsigned long lastTimeUpdate;
    static const unsigned long TIME_UPDATE_INTERVAL = 1000; // 1 секундf
    
    // EQ поля
    static bool eqModeActive;
    static int eqParamIndex;
    static unsigned long eqScreenUntil;
    static bool eqValueChanged;
    static unsigned long eqValueChangeTime;
    
    static unsigned long lastEQDrawTime;
    static const unsigned long EQ_DRAW_INTERVAL = 200;
    
    // Кешування значень для простого EQ екрану
    static int lastBassValue;
    static int lastTrebleValue;

    // Power save / anti burn-in
    static unsigned long lastActivityTime;
    static bool displayDimmed;
    static bool displaySleeping;

    static const unsigned long DIM_TIMEOUT = 60000;       // 1 хв
    static const unsigned long SLEEP_TIMEOUT = 180000;    // 3 хв
    static const uint8_t NORMAL_CONTRAST = 180;
    static const uint8_t DIM_CONTRAST = 5;

public:
    static void initialize() {
        u8g2.begin();
        u8g2.setFlipMode(0);
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.setFontRefHeightExtendedText();
        u8g2.setDrawColor(1);
        u8g2.setBusClock(400000);
        u8g2.setContrast(NORMAL_CONTRAST);
        u8g2.setPowerSave(0);
        
        textPosition = 128;
        directionLeft = true;
        lastStationIndex = -1;
        cachedTime = "--:--:--";
        cachedDate = "--.--.----";
        lastTimeUpdate = 0;
        lastScrollUpdate = 0;
        apModeActive = false;
        
        eqModeActive = false;
        eqParamIndex = 0;
        eqScreenUntil = 0;
        eqValueChanged = false;
        eqValueChangeTime = 0;
        lastEQDrawTime = 0;
        
        lastBassValue = -1;
        lastTrebleValue = -1;

        lastActivityTime = millis();
        displayDimmed = false;
        displaySleeping = false;
        
        Serial.println("Display initialized");
    }

    static void notifyActivity() {
        lastActivityTime = millis();

        if (displaySleeping) {
            u8g2.setPowerSave(0);
            displaySleeping = false;

            lastUpdateTime = 0;
            lastEQDrawTime = 0;
            lastBassValue = -1;
            lastTrebleValue = -1;
        }

        if (displayDimmed) {
            u8g2.setContrast(NORMAL_CONTRAST);
            displayDimmed = false;
        }
    }
    
    static void showAPInfo() {
        notifyActivity();

        String apSSID = "ESP32-Radio";
        String apPassword = "12345678";
        String apIP = "192.168.4.1";
        
        apModeActive = true;
        
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        u8g2.drawStr(28, 12, "AP MODE");
        u8g2.drawStr(5, 28, "SSID:");
        u8g2.drawStr(45, 28, apSSID.c_str());
        u8g2.drawStr(5, 44, "Pass:");
        u8g2.drawStr(45, 44, apPassword.c_str());
        u8g2.drawStr(5, 60, "IP:");
        u8g2.drawStr(28, 60, apIP.c_str());
        u8g2.sendBuffer();
        
        Serial.println("======================================");
        Serial.println("AP MODE ACTIVE - Connect to:");
        Serial.printf("  SSID: %s\n", apSSID.c_str());
        Serial.printf("  Password: %s\n", apPassword.c_str());
        Serial.printf("  IP: %s\n", apIP.c_str());
        Serial.println("======================================");
    }
    
    static void showMessage(const char* message, const String& ip = "") {
        notifyActivity();

        if (String(message) == "WiFi OK") {
            apModeActive = false;
        }
        
        showMsgText = message;
        showMsgIp = ip;
        showMsgActive = true;
        showMsgUntil = millis() + 2000;
        drawMessage();
    }
    
    // ========== ФУНКЦІЇ ДЛЯ EQ РЕЖИМУ ==========
    
    static bool isEQModeActive() {
        return eqModeActive;
    }
    
    static int getEQParamIndex() {
        return eqParamIndex;
    }
    
    static void showEQMode(bool active, int paramIndex) {
        notifyActivity();

        eqModeActive = active;
        eqParamIndex = constrain(paramIndex, 0, 1);
        eqValueChanged = false;
        
        if (active) {
            eqScreenUntil = millis() + 8000;
            lastEQDrawTime = 0;
            lastBassValue = -1;
            lastTrebleValue = -1;
            drawEQScreen();
        } else {
            eqScreenUntil = 0;
            u8g2.setFont(u8g2_font_ncenB08_tr);
            drawScroll();
        }
    }
    
    static void showEQValue(int paramIndex, int value) {
        notifyActivity();

        (void)value;
        eqParamIndex = constrain(paramIndex, 0, 1);
        eqValueChanged = true;
        eqValueChangeTime = millis();
        lastBassValue = -1;
        lastTrebleValue = -1;
    }
    
    static void drawEQScreen() {
        int bassValue = SettingsManager::getBass();
        int trebleValue = SettingsManager::getTreble();
        
        bool needRedraw = (bassValue != lastBassValue) ||
                          (trebleValue != lastTrebleValue);
        
        if (!needRedraw) {
            return;
        }
        
        lastBassValue = bassValue;
        lastTrebleValue = trebleValue;
        
        u8g2.clearBuffer();
        
        bool isBassAmp = (eqParamIndex == 0);
        bool isTrebleAmp = (eqParamIndex == 1);
        
        // ========== BASS ==========
        int bassPercent = (bassValue * 100) / 15;
        
        u8g2.setFont(u8g2_font_ncenB08_tr);
        if (isBassAmp) {
            u8g2.drawStr(3, 20, "BASS<");
        } else {
            u8g2.drawStr(3, 20, "BASS");
        }
        
        u8g2.setFont(u8g2_font_6x10_tr);
        char bassStr[10];
        snprintf(bassStr, sizeof(bassStr), "+%d dB", bassValue); // Додав + для одноманітності
        u8g2.drawStr(5, 34, bassStr);
        
        int bassBarWidth = (bassPercent * 40) / 100;
        u8g2.drawBox(5, 38, bassBarWidth, 5);
        
        // ========== TREBLE ==========
        // Розрахунок відсотків для смужки: від -8 до 7
        int treblePercent = map(trebleValue, -8, 7, 0, 100);
        
        u8g2.setFont(u8g2_font_ncenB08_tr);
        if (isTrebleAmp) {
            u8g2.drawStr(73, 20, ">TREBLE");
        } else {
            u8g2.drawStr(80, 20, "TREBLE");
        }
        
        u8g2.setFont(u8g2_font_6x10_tr);
        char trebleStr[15];
        int trebleScaled = trebleValue * 15; // 1.5 dB крок * 10
        
        // Коректне відображення знаку та десяткових частин для від'ємних чисел
        if (trebleValue > 0) {
            snprintf(trebleStr, sizeof(trebleStr), "+%d.%d dB", trebleScaled / 10, abs(trebleScaled % 10));
        } else {
            snprintf(trebleStr, sizeof(trebleStr), "%d.%d dB", trebleScaled / 10, abs(trebleScaled % 10));
        }
        u8g2.drawStr(75, 34, trebleStr); 
        
        int trebleBarWidth = (treblePercent * 40) / 100;
        u8g2.drawBox(80, 38, trebleBarWidth, 5);
        
        u8g2.sendBuffer();
    }
    
    static void update() {
        unsigned long now = millis();

        // Auto dim / sleep
        unsigned long idleTime = now - lastActivityTime;

        if (!displaySleeping && idleTime >= SLEEP_TIMEOUT) {
            u8g2.setPowerSave(1);
            displaySleeping = true;
            return;
        }

        if (!displayDimmed && idleTime >= DIM_TIMEOUT) {
            u8g2.setContrast(DIM_CONTRAST);
            displayDimmed = true;
        }
        
        if (apModeActive) {
            return;
        }
        
        if (eqModeActive) {
            if (now > eqScreenUntil && !eqValueChanged) {
                eqModeActive = false;
                showMessage("EQ Exit");
                return;
            }
            
            if (eqValueChanged && now - eqValueChangeTime > 3000) {
                eqValueChanged = false;
                eqScreenUntil = millis() + 8000;
            }
            
            if (now - lastEQDrawTime >= EQ_DRAW_INTERVAL) {
                lastEQDrawTime = now;
                drawEQScreen();
            }
            
            vTaskDelay(1);
            return;
        }
        
        if (now - lastUpdateTime < UPDATE_INTERVAL) {
            return;
        }
        lastUpdateTime = now;
        
        if (showMsgActive) {
            if (now > showMsgUntil) {
                showMsgActive = false;
            } else {
                drawMessage();
                return;
            }
        }
        
        drawScroll();
    }

private:
    static void drawMessage() {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_ncenB08_tr);
        
        int textWidth = u8g2.getUTF8Width(showMsgText.c_str());
        int x = (128 - textWidth) / 2;
        if (x < 0) x = 0;
        
        u8g2.drawStr(x, 32, showMsgText.c_str());
        
        if (showMsgIp.length() > 0) {
            int ipWidth = u8g2.getUTF8Width(showMsgIp.c_str());
            int ipX = (128 - ipWidth) / 2;
            if (ipX < 0) ipX = 0;
            u8g2.drawStr(ipX, 48, showMsgIp.c_str());
        }
        
        u8g2.sendBuffer();
    }

        static void drawScroll() {
    unsigned long now = millis();

    u8g2.setFont(u8g2_font_ncenB08_tr);

    const char* name = SettingsManager::getStationName(SettingsManager::getCurrentStation());
    int textWidth = u8g2.getUTF8Width(name);

    // Межі руху:
    // 0 = перший символ торкається лівого краю
    // 128 - textWidth = останній символ торкається правого краю
    int minPos = min(0, 128 - textWidth);
    int maxPos = max(0, 128 - textWidth);

    // Скидаємо скрол при зміні станції
    int currentStation = SettingsManager::getCurrentStation();
    if (currentStation != lastStationIndex) {
        lastStationIndex = currentStation;
        textPosition = 0; // старт від лівого краю

        // Якщо назва довга — спочатку їде вліво
        // Якщо коротка — спочатку їде вправо
        directionLeft = (textWidth > 128);
    }

    if (now - lastScrollUpdate >= SCROLL_INTERVAL) {
        lastScrollUpdate = now;

        if (directionLeft) {
            textPosition--;
            if (textPosition <= minPos) {
                textPosition = minPos;
                directionLeft = false;
            }
        } else {
            textPosition++;
            if (textPosition >= maxPos) {
                textPosition = maxPos;
                directionLeft = true;
            }
        }
    }

    u8g2.clearBuffer();

    u8g2.setCursor(textPosition, 32);
    u8g2.print(name);

    if (now - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
            lastTimeUpdate = now;
            cachedTime = NTPManager::getFormattedTime();
            cachedDate = NTPManager::getFormattedDate();
        }

        u8g2.setCursor(3, 8);
        u8g2.print(cachedTime);
        
        u8g2.setCursor(72, 8);
        u8g2.print(cachedDate);

    u8g2.setCursor(50, 62);
    u8g2.print("Vol: ");
    u8g2.print(SettingsManager::getVolume());

    u8g2.sendBuffer();
    }
};

// Визначення статичних змінних
bool DisplayManager::showMsgActive = false;
unsigned long DisplayManager::showMsgUntil = 0;
String DisplayManager::showMsgText;
String DisplayManager::showMsgIp;
unsigned long DisplayManager::lastUpdateTime = 0;
int DisplayManager::textPosition = 128;
bool DisplayManager::directionLeft = true;
int DisplayManager::lastStationIndex = -1;
unsigned long DisplayManager::lastScrollUpdate = 0;
bool DisplayManager::apModeActive = false;

bool DisplayManager::eqModeActive = false;
int DisplayManager::eqParamIndex = 0;
unsigned long DisplayManager::eqScreenUntil = 0;
bool DisplayManager::eqValueChanged = false;
unsigned long DisplayManager::eqValueChangeTime = 0;
unsigned long DisplayManager::lastEQDrawTime = 0;

int DisplayManager::lastBassValue = -1;
int DisplayManager::lastTrebleValue = -1;

unsigned long DisplayManager::lastActivityTime = 0;
bool DisplayManager::displayDimmed = false;
bool DisplayManager::displaySleeping = false;
String DisplayManager::cachedTime = "--:--:--";
String DisplayManager::cachedDate = "--.--.----";
unsigned long DisplayManager::lastTimeUpdate = 0;

#endif