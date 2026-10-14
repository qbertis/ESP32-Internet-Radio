#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

// ===== Команди для черги =====
enum CommandType {
    CMD_NONE,
    CMD_CONNECT_STATION,
    CMD_SET_VOLUME,
    CMD_NEXT_STATION,
    CMD_PREV_STATION,
    CMD_UPDATE_DISPLAY
};

struct Command {
    CommandType type;
    int value;
    char stationName[32];
};

// ===== Проєктні файли =====
#include "MyWiFiManager.h"
#include "AudioPlayer.h"
#include "DisplayManager.h"
#include "ButtonManager.h"
#include "RadioStream.h"
#include "SettingsManager.h"
#include "NTPManager.h"
#include "WebServerManager.h"

// ===== Глобальні об'єкти =====
WiFiManager wm;
WiFiClient client;
WebServer server(80);

TaskHandle_t audioTaskHandle = NULL;
TaskHandle_t uiTaskHandle = NULL;
QueueHandle_t commandQueue = NULL;
SemaphoreHandle_t serialMutex = NULL;

// ── Serial вивід ──
#define APP_LOG(fmt, ...) \
    if (serialMutex && xSemaphoreTake(serialMutex, pdMS_TO_TICKS(10)) == pdTRUE) { \
        Serial.printf(fmt, ##__VA_ARGS__); \
        xSemaphoreGive(serialMutex); \
    }

// ── Forward declarations ──
void handleSerialCommands();

// ══════════════════════════════════════════════
//  UI TASK — Core 0
// ══════════════════════════════════════════════
static void uiTask(void* parameter) {
    APP_LOG("UI Task started on Core 0\n");
    
    unsigned long lastProfilePrint = 0;
    unsigned long maxLoopTime = 0;
    
    while (true) {
        unsigned long t0 = micros();
        
        server.handleClient();
        ButtonManager::handleButtons();
        DisplayManager::update();
        SettingsManager::tick();
        WiFiManagerClass::checkAndHandle();
        
        unsigned long dt = micros() - t0;
        if (dt > maxLoopTime) maxLoopTime = dt;
        
        if (millis() - lastProfilePrint > 30000) {
            lastProfilePrint = millis();
            APP_LOG("[UI]    loop: %4lu us | max: %4lu us | heap: %5lu | stack: %d\n",
                dt, maxLoopTime,
                ESP.getFreeHeap(),
                uxTaskGetStackHighWaterMark(NULL));
            maxLoopTime = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ══════════════════════════════════════════════
//  AUDIO TASK — Core 1
// ══════════════════════════════════════════════
static void audioTask(void* parameter) {
    APP_LOG("Audio Task started on Core 1\n");
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    AudioPlayer::initialize();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (AudioPlayer::isChipConnected()) {
        APP_LOG("VS1053 connected\n");
    } else {
        APP_LOG("WARNING: VS1053 not detected!\n");
    }
    
    if (WiFiManagerClass::isConnected()) {
        RadioStream::connectToHost();
    }
    
    unsigned long lastProfilePrint = 0;
    unsigned long maxLoopTime = 0;
    
    while (true) {
    // ── Reconnect логіка ──
    if (!WiFiManagerClass::isConnected()) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        continue;
    }

    Command cmd;
    if (commandQueue != NULL && xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
        if (cmd.type == CMD_CONNECT_STATION) {
            APP_LOG("[AUDIO] Got command: Switch station\n");
            RadioStream::connectToHost();
            continue;
        }
    }

    if (!RadioStream::isConnected()) {
        APP_LOG("[AUDIO] Stream lost, reconnecting...\n");
        DisplayManager::showMessage("Reconnecting...");
        RadioStream::connectToHost();
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
    }

    // ── Програвання ──
    unsigned long t0 = micros();
    AudioPlayer::processEQ();
    RadioStream::play();

    unsigned long dt = micros() - t0;
    if (dt > maxLoopTime) maxLoopTime = dt;

    if (millis() - lastProfilePrint > 15000) {
        lastProfilePrint = millis();
        APP_LOG("[AUDIO] loop: %4lu us | max: %4lu us | FIFO: %4d/%d | stack: %d\n",
            dt, maxLoopTime,
            RadioStream::fifoAvailable(), RadioStream::FIFO_SIZE,
            uxTaskGetStackHighWaterMark(NULL));
        maxLoopTime = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ══════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    serialMutex = xSemaphoreCreateMutex();
    
    Serial.println("\n========================================");
    Serial.println("ESP32 Internet Radio");
    Serial.println("========================================\n");
    
    SettingsManager::loadSettings();
    DisplayManager::initialize();
    
    // WiFi — блокуючий виклик
    WiFiManagerClass::connect();
    
    // DNS + NTP тільки після WiFi
    if (WiFiManagerClass::isConnected()) {
        IPAddress dns1(8, 8, 8, 8);
        IPAddress dns2(8, 8, 4, 4);
        WiFi.setDNS(dns1, dns2);
        Serial.println("DNS set: 8.8.8.8 / 8.8.4.4");

    NTPManager::initialize();
    }
    
    WebServerManager::start();
    ButtonManager::initialize();
    
    DisplayManager::showMessage("Starting...", WiFiManagerClass::getIP());
    
    xTaskCreatePinnedToCore(
        audioTask,      // функція
        "AudioTask",    // ім'я
        8192,           // стек — аудіо не потребує багато
        NULL,
        3,              // пріоритет — вищий
        &audioTaskHandle,
        1               // Core 1
    );
    
    xTaskCreatePinnedToCore(
        uiTask,
        "UITask",
        24576,          // стек — більший, бо JSON + WebServer + U8g2
        NULL,
        1,
        &uiTaskHandle,
        0               // Core 0
    );
    
    Serial.println("Setup complete!");
}

// ══════════════════════════════════════════════
//  LOOP — мінімальний, тільки Serial команди
// ══════════════════════════════════════════════
void loop() {
    handleSerialCommands();
    delay(100);
}

// ══════════════════════════════════════════════
//  SERIAL КОМАНДИ
// ══════════════════════════════════════════════
void handleSerialCommands() {
    if (!Serial.available()) return;
    
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    
    if (cmd == "heap") {
        Serial.printf("Free heap: %lu bytes\n", ESP.getFreeHeap());
        Serial.printf("Min heap:  %lu bytes\n", ESP.getMinFreeHeap());
        
    } else if (cmd == "fifo") {
        Serial.printf("FIFO: %d / %d\n",
            RadioStream::fifoAvailable(), RadioStream::FIFO_SIZE);
        
    } else if (cmd == "stack") {
        Serial.printf("Audio stack left: %d words\n",
            uxTaskGetStackHighWaterMark(audioTaskHandle));
        Serial.printf("UI stack left:    %d words\n",
            uxTaskGetStackHighWaterMark(uiTaskHandle));
        
    } else if (cmd == "station") {
        int s = SettingsManager::getCurrentStation();
        Serial.printf("Station %d: %s\n", s + 1,
            SettingsManager::getStationName(s));
        
    } else if (cmd == "wifi") {
        Serial.printf("SSID: %s | IP: %s | RSSI: %d dBm\n",
            WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.RSSI());
        
    } else if (cmd == "eq") {
        Serial.printf("Bass: +%d dB | Treble: +%.1f dB\n",
            SettingsManager::getBass(),
            SettingsManager::getTreble() * 1.5f);
        
    } else if (cmd == "reconnect") {
        Serial.println("Forcing reconnect...");
        RadioStream::connectToHost();
        
    } else if (cmd == "reset") {
        Serial.println("Resetting...");
        delay(500);
        ESP.restart();
        
    } else if (cmd.length() > 0) {
        Serial.println("Commands:");
        Serial.println("  heap      — free memory");
        Serial.println("  fifo      — audio buffer");
        Serial.println("  stack     — task stack usage");
        Serial.println("  station   — current station");
        Serial.println("  wifi      — WiFi status");
        Serial.println("  eq        — EQ settings");
        Serial.println("  reconnect — reconnect stream");
        Serial.println("  reset     — restart ESP32");
    }
}