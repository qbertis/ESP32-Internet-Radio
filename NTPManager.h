#ifndef NTP_MANAGER_H
#define NTP_MANAGER_H

#include <Arduino.h>
#include <time.h>
#include <esp_sntp.h>

class NTPManager {
public:
    static void initialize() {
        // Встановлюємо інтервал синхронізації (1 година = 3600000 мс)
        sntp_set_sync_interval(3600000);  
        
        // Сервери NTP (0, 0 - бо зміщення задаємо через змінну TZ)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        
        // Часовий пояс для України (Київ: EET/EEST)
        setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
        tzset();
    }

    static String getFormattedTime() {
        struct tm timeinfo;
        // Таймаут, щоб не гальмувати інтерфейс, коли немає підключення
        if (!getLocalTime(&timeinfo, 100)) return "--:--:--";

        char buffer[9]; // 8 символів + 1 для '\0'
        strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
        return String(buffer);
    }

    static String getFormattedDate() {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo, 100)) return "--.--.----";

        char buffer[11]; // 10 символів + 1 для '\0'
        strftime(buffer, sizeof(buffer), "%d.%m.%Y", &timeinfo);
        return String(buffer);
    }
};

#endif