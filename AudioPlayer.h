#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <VS1053.h>
#include <SPI.h>
#include "SettingsManager.h"

#define VS1053_CS    5
#define VS1053_DCS   16
#define VS1053_DREQ  4

#define SCI_BASS     0x02
#define SCI_VOL      0x0B

class AudioPlayer {
private:
    inline static VS1053* player = nullptr;
    inline static bool initialized = false;
    inline static volatile bool eqNeedsUpdate = false;

    static void writeRegisterSafe(uint8_t reg, uint16_t value) {
        if (!player) return;

        while (!digitalRead(VS1053_DREQ)) {
            yield();
        }

        digitalWrite(VS1053_CS, LOW);
        SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
        SPI.transfer(0x02);
        SPI.transfer(reg);
        SPI.transfer(value >> 8);
        SPI.transfer(value & 0xFF);
        SPI.endTransaction();
        digitalWrite(VS1053_CS, HIGH);

        delayMicroseconds(30);
    }

    inline static const uint8_t logVolumeTable[101] = {
        0,  18, 22, 26, 30, 34, 37, 40, 42, 44, // 0-9 (швидкий зліт)
        46, 48, 50, 52, 54, 56, 58, 59, 61, 62, // 10-19
        63, 64, 65, 66, 67, 68, 69, 70, 71, 71, // 20-29
        72, 72, 73, 73, 73, 74, 74, 74, 74, 75, // 30-39
        75, 75, 75, 75, 75, 75, 75, 75, 75, 75, // 40-49 (стабілізація перед ривком)
        75, 76, 77, 78, 79, 80, 81, 82, 83, 84, // 50-59 (початок з 75!)
        85, 86, 87, 87, 88, 88, 89, 89, 90, 90, // 60-69
        91, 91, 92, 92, 93, 93, 94, 94, 95, 95, // 70-79
        96, 96, 96, 97, 97, 97, 98, 98, 98, 98, // 80-89
        99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99 // 90-100
    };  

public:
    static void initialize() {
        if (initialized) return;

        SPI.begin();

        player = new VS1053(VS1053_CS, VS1053_DCS, VS1053_DREQ);
        player->begin();
        delay(100);

        uint8_t ver = player->getChipVersion();
        Serial.printf("VS1053 Chip Version: %d\n", ver);

        if (ver == 4) {
            player->loadDefaultVs1053Patches();
            Serial.println("VS1053 patches loaded");
        }

        if (!player->testComm("VS1053 Test")) {
            Serial.println("ERROR: VS1053 communication failed!");
            return;
        }

        Serial.println("VS1053 communication OK");

        player->softReset();
        delay(100);

        player->switchToMp3Mode();
        //player->setVolume(SettingsManager::getVolume());

        initialized = true;
        setVolume(SettingsManager::getVolume());
        applyBassTreble();

        Serial.println("AudioPlayer initialized");
    }

    static void setVolume(int volume) {
        if (player && initialized) {
            int v = constrain(volume, 0, 100);
        
            // Беремо значення з нової таблиці
            uint8_t correctedVol = logVolumeTable[v];
        
            // Запобіжник: якщо в таблиці десь проскочило 100, 
            // або бібліотека глючить на максимумі
            if (correctedVol > 99) correctedVol = 99;
            if (v == 0) correctedVol = 0;

            player->setVolume(correctedVol);
        
            // Для налагодження
            Serial.printf("[Audio] UI: %d%% -> VS1053: %d\n", volume, correctedVol);
        }
    }

    static void stop() {
        if (player && initialized) {
            player->stopSong();
        }
    }

    static bool dataRequest() {
        if (!initialized || !player) return false;
        return player->data_request();
    }

    static void playChunk(const uint8_t* data, size_t len) {
        if (!initialized || !player) return;
        player->playChunk((uint8_t*)data, len);
    }

    static bool isInitialized() {
        return initialized;
    }

    static bool isChipConnected() {
        if (!player) return false;
        return player->testComm("Check");
    }

        inline static bool muted = false;

    static void mute() {
        if (!player || !initialized) return;
        writeRegisterSafe(SCI_VOL, 0xFEFE);
        muted = true;
        Serial.println("[AUDIO] MUTE ON");
    }

    static void unmute() {
        if (!player || !initialized) return;
        muted = false;
        player->setVolume(constrain(SettingsManager::getVolume(), 0, 100));
        Serial.println("[AUDIO] MUTE OFF");
    }

    static bool isMuted() {
        return muted;
    }

    static void applyBassTreble() {
    if (!initialized) return;

    int bassAmp = constrain(SettingsManager::getBass(), BASS_AMP_MIN, BASS_AMP_MAX);
    int trebAmp = constrain(SettingsManager::getTreble(), TREBLE_AMP_MIN, TREBLE_AMP_MAX);

    // Підготовка Treble (4 біти). 
    // Якщо значення від'ємне, воно має бути у форматі Two's Complement.
    // Наприклад: -1 це 0b1111 (F), -8 це 0b1000 (8).
    uint16_t tBits = (uint16_t)trebAmp & 0x0F; 

    uint16_t sciBassBits =
        (tBits << 12)                          | // Treble Gain
        ((TREBLE_FREQ_FIXED & 0x0F) << 8)      | // Treble Freq (7 = 7kHz)
        ((bassAmp & 0x0F) << 4)                | // Bass Gain
        (BASS_FREQ_FIXED & 0x0F);                // Bass Freq (12 = 120Hz)

    writeRegisterSafe(SCI_BASS, sciBassBits);

    // Вивід у консоль для контролю
    Serial.printf("EQ Update: SCI_BASS=0x%04X | Bass: %d dB | Treble: %.1f dB\n",
                  sciBassBits, bassAmp, trebAmp * 1.5f);
    }

    static int getBassGainDb(int regValue) {
        return constrain(regValue, BASS_AMP_MIN, BASS_AMP_MAX);
    }

    static float getTrebleGainDb(int regValue) {
    // Дозволяємо повертати від'ємні числа
        return constrain(regValue, TREBLE_AMP_MIN, TREBLE_AMP_MAX) * 1.5f;
    }

    static void updateEQ() {
        eqNeedsUpdate = true;
    }

    static void processEQ() {
        if (eqNeedsUpdate) {
            eqNeedsUpdate = false;
            applyBassTreble();
        }
    }
};

#endif // AUDIO_PLAYER_H