#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#define MAX_STATIONS 200
#define CONFIG_FILE "/radio_config.json"

// Простий EQ:
/*ST_AMPLITUDE (біти 15-12): Treble gain в 1.5 dB кроках
    - 0000 = off
    - 0001 = +1.5 dB
    - 0111 = +10.5 dB
    - 1000 = -12 dB (signed! two's complement)
    - 1111 = -1.5 dB

ST_FREQLIMIT (біти 11-8): нижня частота treble, value × 1000 Hz
    - 1 = 1 kHz
    - 15 = 15 kHz

SB_AMPLITUDE (біти 7-4): Bass boost в 1 dB кроках
    - 0 = off
    - 1 = +1 dB
    - 15 = +15 dB

SB_FREQLIMIT (біти 3-0): верхня частота bass, value × 10 Hz
    - 2 = 20 Hz
    - 15 = 150 Hz*/

#define BASS_AMP_MIN        0
#define BASS_AMP_MAX        15
#define TREBLE_AMP_MIN      -8
#define TREBLE_AMP_MAX      7

#define BASS_FREQ_FIXED     12   // 150 Hz  (15 * 10 Hz)
#define TREBLE_FREQ_FIXED   7  // 10 kHz (10 * 1000 Hz)

struct RadioStation {
    char name[32];
    char host[64];
    char path[128];
};

class SettingsManager {
public:
    static RadioStation stations[MAX_STATIONS];
    static int totalStations;
    static int currentStation;
    static int volume;

    // EQ — тільки gain, частоти фіксовані в AudioPlayer
    static int bassBoost;    // 0-15 dB, крок 1 dB
    static int trebleBoost;  // -8-7,  крок 1.5 dB

    static bool settingsChanged;
    static unsigned long lastSaveTime;
    static const unsigned long SAVE_DELAY = 2000;

    // ---------------------- LOAD ----------------------
    static void loadSettings() {
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS mount failed");
            loadDefaults();
            return;
        }

        if (!SPIFFS.exists(CONFIG_FILE)) {
            Serial.println("Config file not found, creating defaults");
            loadDefaults();
            saveAll();
            return;
        }

        File file = SPIFFS.open(CONFIG_FILE, FILE_READ);
        if (!file) {
            Serial.println("Failed to open config file");
            loadDefaults();
            return;
        }

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) {
            Serial.println("Failed to parse config file");
            loadDefaults();
            return;
        }

        totalStations  = doc["totalStations"]  | 0;
        currentStation = doc["currentStation"] | 0;
        volume         = doc["volume"]         | 100;

        // EQ — тільки два параметри
        bassBoost   = doc["bassBoost"]   | 0;
        trebleBoost = doc["trebleBoost"] | 0;

        // Обмежуємо значення
        if (totalStations > MAX_STATIONS) totalStations = MAX_STATIONS;
        if (currentStation >= totalStations) currentStation = 0;
        volume      = constrain(volume,      0, 100);
        bassBoost   = constrain(bassBoost,   BASS_AMP_MIN,   BASS_AMP_MAX);
        trebleBoost = constrain(trebleBoost, TREBLE_AMP_MIN, TREBLE_AMP_MAX);

        // Завантажуємо станції
        for (int i = 0; i < totalStations; i++) {
            String key = "station_" + String(i);
            JsonObject obj = doc[key];
            if (obj) {
                strlcpy(stations[i].name, obj["name"] | "", sizeof(stations[i].name));
                strlcpy(stations[i].host, obj["host"] | "", sizeof(stations[i].host));
                strlcpy(stations[i].path, obj["path"] | "", sizeof(stations[i].path));
            }
        }

        Serial.printf("Loaded %d stations\n", totalStations);
        Serial.printf("EQ: Bass=%d dB, Treble=%.1f dB\n",
                      bassBoost, trebleBoost * 1.5f);

        if (totalStations == 0) {
            addStation("Radio Roks", "online.radioroks.ua", "/RadioROKS");
        }
    }

    // ---------------------- SAVE ALL ----------------------
    static void saveAll() {
        JsonDocument doc;

        doc["totalStations"]  = totalStations;
        doc["currentStation"] = currentStation;
        doc["volume"]         = volume;
        doc["bassBoost"]      = bassBoost;
        doc["trebleBoost"]    = trebleBoost;

        for (int i = 0; i < totalStations; i++) {
            String key = "station_" + String(i);
            JsonObject obj = doc[key].to<JsonObject>();
            obj["name"] = stations[i].name;
            obj["host"] = stations[i].host;
            obj["path"] = stations[i].path;
        }

        File file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
        if (!file) {
            Serial.println("Failed to open config file for writing");
            return;
        }

        if (serializeJson(doc, file) == 0) {
            Serial.println("Failed to write config file");
        } else {
            Serial.println("Settings saved to SPIFFS");
        }

        file.close();
    }

    // ---------------------- DEFAULTS ----------------------
    static void loadDefaults() {
        totalStations  = 1;
        currentStation = 0;
        volume         = 100;

        // EQ вимкнено за замовчуванням (flat)
        bassBoost   = 0;
        trebleBoost = 0;

        strlcpy(stations[0].name, "Radio Roks", sizeof(stations[0].name));
        strlcpy(stations[0].host, "online.radioroks.ua", sizeof(stations[0].host));
        strlcpy(stations[0].path, "/RadioROKS", sizeof(stations[0].path));

        Serial.println("Default settings loaded");
    }

    // ---------------------- STATION CRUD ----------------------
    static void addStation(const char* name, const char* host, const char* path) {
    if (totalStations >= MAX_STATIONS) return;

    strlcpy(stations[totalStations].name, name, sizeof(stations[totalStations].name));
    strlcpy(stations[totalStations].host, host, sizeof(stations[totalStations].host));
    strlcpy(stations[totalStations].path, path, sizeof(stations[totalStations].path));

    totalStations++;
    markChanged();
    }

    static void deleteStation(int index) {
        if (index < 0 || index >= totalStations) return;
        for (int i = index; i < totalStations - 1; i++) {
            stations[i] = stations[i + 1];
        }
        totalStations--;
        if (currentStation >= totalStations) currentStation = 0;
        markChanged();
    }

   static void updateStation(int index, const char* name,
                          const char* host, const char* path) {
    if (index < 0 || index >= totalStations) return;
    strlcpy(stations[index].name, name, sizeof(stations[index].name));
    strlcpy(stations[index].host, host, sizeof(stations[index].host));
    strlcpy(stations[index].path, path, sizeof(stations[index].path));
    markChanged();
    }

    // ---------------------- GETTERS ----------------------
    static const char* getStationName(int i) {
        return (i >= 0 && i < totalStations) ? stations[i].name : "Unknown";
    }

    static const char* getStationHost(int i) {
        return (i >= 0 && i < totalStations) ? stations[i].host : "";
    }

    static const char* getStationPath(int i) {
        return (i >= 0 && i < totalStations) ? stations[i].path : "";
    }

    static int getCurrentStation() { return currentStation; }

    static void nextStation() {
        currentStation = (currentStation + 1) % totalStations;
        markChanged();
    }

    static void previousStation() {
        currentStation = (currentStation - 1 + totalStations) % totalStations;
        markChanged();
    }

    static int getVolume() { return volume; }

    static void setVolume(int v) {
        v = constrain(v, 0, 100);
        if (volume != v) {
            volume = v;
            markChanged();
        }
    }

    // ---------------------- EQ GETTERS / SETTERS ----------------------
    static int getBass()   { return bassBoost; }
    static int getTreble() { return trebleBoost; }

    static void setBass(int value) {
        value = constrain(value, BASS_AMP_MIN, BASS_AMP_MAX);
        if (bassBoost != value) {
            bassBoost = value;
            markChanged();
        }
    }

    static void setTreble(int value) {
        value = constrain(value, TREBLE_AMP_MIN, TREBLE_AMP_MAX);
        if (trebleBoost != value) {
            trebleBoost = value;
            markChanged();
        }
    }

    // ---------------------- AUTO-SAVE ----------------------
    static void markChanged() {
        settingsChanged = true;
        lastSaveTime = millis();
    }

    static void tick() {
        if (settingsChanged && (millis() - lastSaveTime > SAVE_DELAY)) {
            saveAll();
            settingsChanged = false;
            Serial.println("Settings auto-saved");
        }
    }
};

// Ініціалізація статичних змінних
RadioStation SettingsManager::stations[MAX_STATIONS];
int SettingsManager::totalStations  = 0;
int SettingsManager::currentStation = 0;
int SettingsManager::volume         = 100;
int SettingsManager::bassBoost      = 0;
int SettingsManager::trebleBoost    = 0;
bool SettingsManager::settingsChanged  = false;
unsigned long SettingsManager::lastSaveTime = 0;

#endif