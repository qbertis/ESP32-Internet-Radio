#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "AudioPlayer.h"
#include "SettingsManager.h"

extern WiFiClient client;

class RadioStream {
public:
  static const int FIFO_SIZE = 16384;
  static const unsigned long DATA_TIMEOUT = 10000;
  static const unsigned long CONNECT_TIMEOUT = 10000;

private:
  static uint8_t fifo[FIFO_SIZE];
  static volatile int fifoHead;
  static volatile int fifoTail;
  static portMUX_TYPE fifoMux;

  static unsigned long lastDataTime;
  static volatile bool streamActive;
  static volatile bool isConnecting;
  static unsigned long lastDebugPrint;
  static int reconnectAttempts;

public:
  static void connectToHost() {
    if (isConnecting) {
      Serial.println("[STREAM] Already connecting, skip");
      return;
    }

    AudioPlayer::setVolume(0);

    isConnecting = true;
    streamActive = false;

    unsigned long startTime = millis();

    int station = SettingsManager::getCurrentStation();
    const char* rawHost = SettingsManager::getStationHost(station);
    const char* path = SettingsManager::getStationPath(station);
    const char* name = SettingsManager::getStationName(station);

    // Парсимо порт з хоста (формат: "host:port" або просто "host")
    char host[64];
    uint16_t port = 80;
    parseHostPort(rawHost, host, sizeof(host), port);

    Serial.printf("\n[STREAM] Connecting: %s (%s:%d%s)\n",
                  name, host, port, path);

    AudioPlayer::stop();

    if (client.connected()) {
      client.stop();
      client.clear();
      vTaskDelay(pdMS_TO_TICKS(200));
    }

    portENTER_CRITICAL(&fifoMux);
    fifoHead = 0;
    fifoTail = 0;
    portEXIT_CRITICAL(&fifoMux);

    lastDataTime = millis();

    IPAddress serverIP;
    if (!WiFi.hostByName(host, serverIP)) {
      Serial.printf("[STREAM] DNS failed for: %s\n", host);
      reconnectAttempts++;
      if (reconnectAttempts >= 3) {
        Serial.println("[STREAM] Switching station...");
        reconnectAttempts = 0;
        SettingsManager::nextStation();
      }
      isConnecting = false;
      return;
    }

    Serial.printf("[STREAM] DNS: %s -> %s\n", host, serverIP.toString().c_str());

    bool connected = false;
    for (int attempt = 1; attempt <= 3 && !connected; attempt++) {
      Serial.printf("[STREAM] Attempt %d/3...\n", attempt);
      client.setTimeout(CONNECT_TIMEOUT / 1000);
      if (client.connect(serverIP, port)) {
        connected = true;
        Serial.printf("[STREAM] TCP connected (%lu ms)\n", millis() - startTime);
      } else {
        vTaskDelay(pdMS_TO_TICKS(500));
      }
    }

    if (!connected) {
      Serial.println("[STREAM] Connection FAILED");
      reconnectAttempts++;
      if (reconnectAttempts >= 3) {
        Serial.println("[STREAM] Switching station...");
        reconnectAttempts = 0;
        SettingsManager::nextStation();
      }
      isConnecting = false;
      return;
    }

    if (port == 80) {
      client.printf("GET %s HTTP/1.0\r\n"
                    "Host: %s\r\n"
                    "User-Agent: ESP32-Radio/1.0\r\n"
                    "Icy-MetaData: 0\r\n"
                    "Accept: audio/mpeg, audio/*\r\n"
                    "Connection: close\r\n\r\n",
                    path, host);
    } else {
      client.printf("GET %s HTTP/1.0\r\n"
                    "Host: %s:%d\r\n"
                    "User-Agent: ESP32-Radio/1.0\r\n"
                    "Icy-MetaData: 0\r\n"
                    "Accept: audio/mpeg, audio/*\r\n"
                    "Connection: close\r\n\r\n",
                    path, host, port);
    }

    bool headersEnded = false;
    char hdr[4] = { 0 };
    unsigned long headerStart = millis();

    while (client.connected() && !headersEnded) {
      if (client.available()) {
        char c = client.read();

        hdr[0] = hdr[1];
        hdr[1] = hdr[2];
        hdr[2] = hdr[3];
        hdr[3] = c;

        if (hdr[0] == '\r' && hdr[1] == '\n' && hdr[2] == '\r' && hdr[3] == '\n') {
          headersEnded = true;
          break;
        }
      }

      if (millis() - headerStart > 5000) {
        Serial.println("[STREAM] Header timeout");
        break;
      }

      vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!headersEnded) {
      Serial.println("[STREAM] Failed to parse headers");
      client.stop();
      isConnecting = false;
      return;
    }

    streamActive = true;
    lastDataTime = millis();
    reconnectAttempts = 0;

    // --- ПЛАВНЕ НАРОСТАННЯ ГУЧНОСТІ (Fade-in) ---
    int targetVol = SettingsManager::getVolume();
    Serial.printf("[STREAM] Starting fade-in to %d%%\n", targetVol);

    // Починаємо з 0 і крокуємо до цільового значення
    for (int v = 0; v <= targetVol; v++) {
      AudioPlayer::setVolume(v);

      // Швидкість зростання:
      // 10мс * 100 кроків = 1 секунда на повне зростання.
      vTaskDelay(pdMS_TO_TICKS(15));
    }

    Serial.printf("[STREAM] Ready! (%lu ms total)\n", millis() - startTime);

    isConnecting = false;
  }

  static void play() {
    if (isConnecting) return;

    if (!streamActive) {
      if (millis() - lastDataTime > 3000) {
        Serial.println("[STREAM] Inactive, reconnecting...");
        connectToHost();
      }
      return;
    }

    if (!client.connected()) {
      Serial.println("[STREAM] Client disconnected");
      streamActive = false;
      return;
    }

    int available = client.available();
    int freeSpace = fifoFree();

    if (available > 0 && freeSpace > 0) {
      int toRead = min(available, min(freeSpace, 256));
      uint8_t buffer[256];
      int bytesRead = client.read(buffer, toRead);

      if (bytesRead > 0) {
        fifoPush(buffer, bytesRead);
        lastDataTime = millis();
      }
    }

    int bytesSent = 0;
    while (fifoAvailable() >= 64 && bytesSent < 512) {
      uint8_t chunk[32];
      int got = fifoPop(chunk, sizeof(chunk));
      if (got <= 0) break;

      AudioPlayer::playChunk(chunk, got);
      bytesSent += got;
    }

    if (millis() - lastDataTime > DATA_TIMEOUT) {
      Serial.printf("[STREAM] Data timeout: %lu ms\n", millis() - lastDataTime);
      streamActive = false;
      return;
    }

    if (millis() - lastDebugPrint > 30000) {
      lastDebugPrint = millis();
      Serial.printf("[STREAM] FIFO: %d/%d | RSSI: %d | Active: %s | Conn: %s\n",
                    fifoAvailable(), FIFO_SIZE, WiFi.RSSI(),
                    streamActive ? "YES" : "NO",
                    client.connected() ? "YES" : "NO");
    }
  }

  static bool isConnected() {
    return streamActive && client.connected() && !isConnecting;
  }

  static int fifoFree() {
    int used = fifoAvailable();
    return FIFO_SIZE - used - 1;
  }

  static int fifoAvailable() {
    int head, tail;

    portENTER_CRITICAL(&fifoMux);
    head = fifoHead;
    tail = fifoTail;
    portEXIT_CRITICAL(&fifoMux);

    if (head >= tail) {
      return head - tail;
    } else {
      return FIFO_SIZE - (tail - head);
    }
  }

private:
  static void fifoPush(const uint8_t* data, int len) {
    portENTER_CRITICAL(&fifoMux);

    for (int i = 0; i < len; i++) {
      int nextHead = (fifoHead + 1) % FIFO_SIZE;
      if (nextHead == fifoTail) {
        break;
      }
      fifo[fifoHead] = data[i];
      fifoHead = nextHead;
    }

    portEXIT_CRITICAL(&fifoMux);
  }

  static int fifoPop(uint8_t* out, int maxLen) {
    int cnt = 0;

    portENTER_CRITICAL(&fifoMux);

    while (cnt < maxLen && fifoTail != fifoHead) {
      out[cnt++] = fifo[fifoTail];
      fifoTail = (fifoTail + 1) % FIFO_SIZE;
    }

    portEXIT_CRITICAL(&fifoMux);
    return cnt;
  }

  static void parseHostPort(const char* input, char* host, size_t hostSize, uint16_t& port) {
    const char* colon = strchr(input, ':');

    if (colon != NULL) {
      size_t hostLen = colon - input;
      if (hostLen >= hostSize) hostLen = hostSize - 1;
      strncpy(host, input, hostLen);
      host[hostLen] = '\0';

      int p = atoi(colon + 1);
      if (p > 0 && p <= 65535) {
        port = (uint16_t)p;
      }
    } else {
      strncpy(host, input, hostSize - 1);
      host[hostSize - 1] = '\0';
    }
  }
};

uint8_t RadioStream::fifo[RadioStream::FIFO_SIZE];
volatile int RadioStream::fifoHead = 0;
volatile int RadioStream::fifoTail = 0;
portMUX_TYPE RadioStream::fifoMux = portMUX_INITIALIZER_UNLOCKED;
unsigned long RadioStream::lastDataTime = 0;
volatile bool RadioStream::streamActive = false;
volatile bool RadioStream::isConnecting = false;
unsigned long RadioStream::lastDebugPrint = 0;
int RadioStream::reconnectAttempts = 0;

#endif