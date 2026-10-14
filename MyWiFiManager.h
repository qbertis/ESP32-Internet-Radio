#ifndef MY_WIFI_MANAGER_H
#define MY_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "DisplayManager.h"

extern WiFiManager wm;

class WiFiManagerClass {
private:
    static bool wifiConnected;
    static unsigned long lastReconnectAttempt;
    static const unsigned long RECONNECT_INTERVAL = 10000; // 10 сек

public:
    static void connect() {
        Serial.println("\n========== WiFi MANAGER ==========");

        wm.setConfigPortalTimeout(180);

        const char* apSSID = "ESP32-Radio";
        const char* apPassword = "12345678";

        wm.setMinimumSignalQuality(10);
        wm.setConnectTimeout(30);
        wm.setDebugOutput(true);

        WiFi.setAutoReconnect(true);

        Serial.println("Starting WiFi connection...");
        Serial.printf("If connection fails, AP Mode: SSID=%s, Password=%s\n", apSSID, apPassword);

        DisplayManager::showMessage("WiFi Connect...");

        bool connected = wm.autoConnect(apSSID, apPassword);

        if (connected) {
            wifiConnected = true;
            Serial.println("\n✅ WiFi Connected Successfully!");
            Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
            Serial.printf("Signal Strength (RSSI): %d dBm\n", WiFi.RSSI());
            DisplayManager::showMessage("WiFi OK", WiFi.localIP().toString());
        } else {
            wifiConnected = false;
            Serial.println("\n❌ Failed to connect to WiFi");
            Serial.println("Device is running in AP mode only");
            Serial.printf("Connect to AP: %s (password: %s)\n", apSSID, apPassword);
            Serial.println("Then open browser to http://192.168.4.1");
            DisplayManager::showAPInfo();
        }

        Serial.println("==================================\n");
    }

    static bool isConnected() {
        return WiFi.status() == WL_CONNECTED;
    }

    static void resetSettings() {
        Serial.println("Resetting WiFi settings...");
        wm.resetSettings();
        DisplayManager::showMessage("WiFi Reset", "Rebooting...");
        delay(500);
        ESP.restart();
    }

    static void checkAndHandle() {
        bool nowConnected = (WiFi.status() == WL_CONNECTED);

        if (!nowConnected && wifiConnected) {
            Serial.println("WiFi connection lost!");
            wifiConnected = false;
            DisplayManager::showMessage("WiFi Lost");
        }

        if (nowConnected && !wifiConnected) {
            wifiConnected = true;
            Serial.println("WiFi reconnected!");
            DisplayManager::showMessage("WiFi OK", WiFi.localIP().toString());
        }

        if (!nowConnected && millis() - lastReconnectAttempt > RECONNECT_INTERVAL) {
            lastReconnectAttempt = millis();
            Serial.println("Trying WiFi reconnect...");
            WiFi.reconnect();
        }

        wm.process();
    }

    static String getIP() {
        if (isConnected()) {
            return WiFi.localIP().toString();
        }
        return "192.168.4.1";
    }

    static int getRSSI() {
        if (isConnected()) {
            return WiFi.RSSI();
        }
        return 0;
    }
};

bool WiFiManagerClass::wifiConnected = false;
unsigned long WiFiManagerClass::lastReconnectAttempt = 0;

#endif