#ifndef RADIO_STREAM_H
#define RADIO_STREAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <Client.h>

#include "AudioPlayer.h"
#include "SettingsManager.h"


class RadioStream {

public:

    // =========================================================
    // Configuration
    // =========================================================

    static const int FIFO_SIZE = 16384;

    static const unsigned long DATA_TIMEOUT    = 10000UL;
    static const unsigned long CONNECT_TIMEOUT = 10000UL;
    static const unsigned long HEADER_TIMEOUT  = 10000UL;

    static const int MAX_REDIRECTS = 5;

    // Long enough for modern HTTPS URLs with query/JWT parameters.
    static const size_t MAX_URL_LEN  = 1536;
    static const size_t MAX_HOST_LEN = 128;
    static const size_t MAX_PATH_LEN = 1400;
    static const size_t MAX_HEADER_LEN = 4096;


private:

    // =========================================================
    // Network clients
    // =========================================================

    static WiFiClient httpClient;
    static WiFiClientSecure httpsClient;
    static Client* activeClient;

    static bool secureConnection;

    // =========================================================
    // Current URL
    // =========================================================

    static char currentHost[MAX_HOST_LEN];
    static char currentPath[MAX_PATH_LEN];
    static uint16_t currentPort;

    // =========================================================
    // HTTP response
    // =========================================================

    static int currentStatusCode;
    static bool chunkedTransfer;
    static bool icyStream;

    static char responseHeaders[MAX_HEADER_LEN];
    static char redirectLocation[MAX_URL_LEN];

    // =========================================================
    // FIFO
    // =========================================================

    static uint8_t fifo[FIFO_SIZE];
    static volatile int fifoHead;
    static volatile int fifoTail;
    static portMUX_TYPE fifoMux;

    // =========================================================
    // Stream state
    // =========================================================

    static unsigned long lastDataTime;
    static volatile bool streamActive;
    static volatile bool isConnecting;
    static unsigned long lastDebugPrint;
    static int reconnectAttempts;

    // =========================================================
    // Chunked transfer state
    // =========================================================

    static size_t chunkRemaining;
    static bool chunkFinished;
    static uint8_t chunkBuffer[512];


public:

    // =========================================================
    // CONNECT
    // =========================================================

    static void connectToHost() {

        if (isConnecting) {
            Serial.println("[STREAM] Already connecting, skip");
            return;
        }

        isConnecting = true;
        streamActive = false;

        AudioPlayer::setVolume(0);
        AudioPlayer::stop();

        resetFifo();
        stopConnection();

        unsigned long startTime = millis();

        int station = SettingsManager::getCurrentStation();

        const char* rawHost =
            SettingsManager::getStationHost(station);

        const char* rawPath =
            SettingsManager::getStationPath(station);

        const char* name =
            SettingsManager::getStationName(station);

        char url[MAX_URL_LEN];

        if (!buildInitialUrl(rawHost, rawPath, url, sizeof(url))) {

            Serial.println("[STREAM] Invalid station URL");

            isConnecting = false;
            return;
        }

        Serial.println();
        Serial.println("========================================");

        Serial.printf(
            "[STREAM] Station: %s\n",
            name
        );

        Serial.printf(
            "[STREAM] URL: %s\n",
            url
        );

        // =====================================================
        // Connect URL, including redirects
        // =====================================================

        if (!connectUrlWithRedirects(url, MAX_REDIRECTS)) {

            Serial.println("[STREAM] Stream connection FAILED");

            reconnectAttempts++;

            if (reconnectAttempts >= 3) {

                Serial.println(
                    "[STREAM] Switching station..."
                );

                reconnectAttempts = 0;
                SettingsManager::nextStation();
            }

            isConnecting = false;
            return;
        }

        // =====================================================
        // Stream is now valid
        // =====================================================

        streamActive = true;
        lastDataTime = millis();
        reconnectAttempts = 0;

        // =====================================================
        // Fade in
        // =====================================================

        int targetVolume =
            SettingsManager::getVolume();

        Serial.printf(
            "[STREAM] Starting fade-in to %d%%\n",
            targetVolume
        );

        for (int v = 0; v <= targetVolume; v++) {

            AudioPlayer::setVolume(v);

            vTaskDelay(
                pdMS_TO_TICKS(15)
            );
        }

        Serial.printf(
            "[STREAM] READY (%lu ms)\n",
            millis() - startTime
        );

        Serial.println("========================================");

        isConnecting = false;
    }


    // =========================================================
    // PLAY / PROCESS STREAM
    // =========================================================

    static void play() {

        if (isConnecting)
            return;

        // -----------------------------------------------------
        // No active stream
        // -----------------------------------------------------

        if (!streamActive) {

            if (millis() - lastDataTime > 3000UL) {

                Serial.println(
                    "[STREAM] Inactive, reconnecting..."
                );

                connectToHost();
            }

            return;
        }

        // -----------------------------------------------------
        // Connection lost
        // -----------------------------------------------------

        if (
            activeClient == nullptr ||
            !activeClient->connected()
        ) {

            Serial.println(
                "[STREAM] Client disconnected"
            );

            streamActive = false;

            stopConnection();

            return;
        }

        // -----------------------------------------------------
        // Read network data
        // -----------------------------------------------------

        int freeSpace = fifoFree();

        if (freeSpace > 0) {

            int bytesRead =
                readStreamData(
                    min(freeSpace, 512)
                );

            if (bytesRead > 0) {
                lastDataTime = millis();
            }
        }

        // -----------------------------------------------------
        // Feed VS1053
        // -----------------------------------------------------

        int bytesSent = 0;

        while (
            fifoAvailable() >= 64 &&
            bytesSent < 512
        ) {

            uint8_t chunk[32];

            int got =
                fifoPop(
                    chunk,
                    sizeof(chunk)
                );

            if (got <= 0)
                break;

            AudioPlayer::playChunk(
                chunk,
                got
            );

            bytesSent += got;
        }

        // -----------------------------------------------------
        // Network timeout
        // -----------------------------------------------------

        if (
            millis() - lastDataTime >
            DATA_TIMEOUT
        ) {

            Serial.printf(
                "[STREAM] DATA TIMEOUT: %lu ms\n",
                millis() - lastDataTime
            );

            streamActive = false;

            stopConnection();

            return;
        }

        // -----------------------------------------------------
        // Periodic diagnostics
        // -----------------------------------------------------

        if (
            millis() - lastDebugPrint >
            30000UL
        ) {

            lastDebugPrint = millis();

            Serial.printf(
                "[STREAM] FIFO: %d/%d | RSSI: %d | "
                "HTTP: %d | Protocol: %s | "
                "Chunked: %s\n",

                fifoAvailable(),
                FIFO_SIZE,
                WiFi.RSSI(),
                currentStatusCode,

                secureConnection
                    ? "HTTPS"
                    : "HTTP",

                chunkedTransfer
                    ? "YES"
                    : "NO"
            );
        }
    }


    // =========================================================
    // CONNECTION STATUS
    // =========================================================

    static bool isConnected() {

        return
            streamActive &&
            activeClient != nullptr &&
            activeClient->connected() &&
            !isConnecting;
    }


    // =========================================================
    // FIFO
    // =========================================================

    static int fifoAvailable() {

        int head;
        int tail;

        portENTER_CRITICAL(&fifoMux);

        head = fifoHead;
        tail = fifoTail;

        portEXIT_CRITICAL(&fifoMux);

        if (head >= tail)
            return head - tail;

        return FIFO_SIZE - (tail - head);
    }


    static int fifoFree() {

        return FIFO_SIZE -
               fifoAvailable() -
               1;
    }


private:

    // =========================================================
    // URL CONSTRUCTION
    // =========================================================

    static bool buildInitialUrl(
        const char* rawHost,
        const char* rawPath,
        char* out,
        size_t outSize
    ) {

        if (
            rawHost == nullptr ||
            rawPath == nullptr ||
            out == nullptr ||
            outSize == 0
        ) {
            return false;
        }

        // Full URL in Host field.
        //
        // Example:
        // https://stream.example.com/stream?token=123

        if (
            startsWith(rawHost, "http://") ||
            startsWith(rawHost, "https://")
        ) {

            const char* schemeEnd =
                startsWith(rawHost, "https://")
                    ? rawHost + 8
                    : rawHost + 7;

            // URL already contains a path/query.
            if (strpbrk(schemeEnd, "/?#") != nullptr) {

                return safeCopy(
                    out,
                    outSize,
                    rawHost
                );
            }

            // Only scheme + hostname.
            int written = snprintf(
                out,
                outSize,
                "%s%s",
                rawHost,
                rawPath
            );

            return written >= 0 &&
                   (size_t)written < outSize;
        }

        // Legacy format:
        //
        // Host: radio.example.com
        // Path: /stream

        int written = snprintf(
            out,
            outSize,
            "http://%s%s",
            rawHost,
            rawPath
        );

        return written >= 0 &&
               (size_t)written < outSize;
    }


    // =========================================================
    // CONNECT URL + REDIRECTS
    // =========================================================

    static bool connectUrlWithRedirects(
        const char* startUrl,
        int maxRedirects
    ) {

        char url[MAX_URL_LEN];

        if (!safeCopy(
            url,
            sizeof(url),
            startUrl
        )) {
            return false;
        }

        for (
            int redirect = 0;
            redirect <= maxRedirects;
            redirect++
        ) {

            Serial.printf(
                "[STREAM] URL attempt %d/%d\n",
                redirect + 1,
                maxRedirects + 1
            );

            if (!parseUrl(url)) {

                Serial.println(
                    "[STREAM] URL parse failed"
                );

                return false;
            }

            if (!openConnection()) {

                return false;
            }

            sendHttpRequest();

            int status =
                readHttpHeaders();

            currentStatusCode = status;

            Serial.printf(
                "[STREAM] HTTP status: %d\n",
                status
            );

            // -------------------------------------------------
            // Successful stream
            // -------------------------------------------------

            if (status == 200) {

                Serial.printf(
                    "[STREAM] Stream accepted: "
                    "%s://%s:%u%s\n",

                    secureConnection
                        ? "https"
                        : "http",

                    currentHost,
                    currentPort,
                    currentPath
                );

                return true;
            }

            // -------------------------------------------------
            // Redirect
            // -------------------------------------------------

            if (
                status == 301 ||
                status == 302 ||
                status == 303 ||
                status == 307 ||
                status == 308
            ) {

                if (redirectLocation[0] == '\0') {

                    Serial.println(
                        "[STREAM] Redirect without Location"
                    );

                    stopConnection();

                    return false;
                }

                Serial.printf(
                    "[STREAM] Location: %s\n",
                    redirectLocation
                );

                char nextUrl[MAX_URL_LEN];

                if (!resolveRedirect(
                    redirectLocation,
                    nextUrl,
                    sizeof(nextUrl)
                )) {

                    Serial.println(
                        "[STREAM] Cannot resolve redirect"
                    );

                    stopConnection();

                    return false;
                }

                Serial.printf(
                    "[STREAM] Redirect -> %s\n",
                    nextUrl
                );

                stopConnection();

                if (!safeCopy(
                    url,
                    sizeof(url),
                    nextUrl
                )) {
                    return false;
                }

                continue;
            }

            // -------------------------------------------------
            // Authentication/access denied
            // -------------------------------------------------

            if (
                status == 401 ||
                status == 403
            ) {

                Serial.printf(
                    "[STREAM] Server rejected request: "
                    "HTTP %d\n",
                    status
                );

                Serial.println(
                    "[STREAM] The stream URL may require "
                    "a fresh token."
                );

                stopConnection();

                return false;
            }

            // -------------------------------------------------
            // Other error
            // -------------------------------------------------

            Serial.printf(
                "[STREAM] HTTP ERROR: %d\n",
                status
            );

            stopConnection();

            return false;
        }

        Serial.println(
            "[STREAM] Too many redirects"
        );

        return false;
    }


    // =========================================================
    // URL PARSER
    // =========================================================

    static bool parseUrl(
        const char* url
    ) {

        if (url == nullptr)
            return false;

        secureConnection = false;

        const char* p;

        if (startsWith(url, "https://")) {

            secureConnection = true;
            p = url + 8;

        } else if (startsWith(url, "http://")) {

            secureConnection = false;
            p = url + 7;

        } else {

            Serial.println(
                "[STREAM] URL must start with "
                "http:// or https://"
            );

            return false;
        }

        // -----------------------------------------------------
        // Find end of authority
        // -----------------------------------------------------

        const char* authorityEnd =
            strpbrk(p, "/?#");

        size_t authorityLen;

        if (authorityEnd != nullptr)
            authorityLen = authorityEnd - p;
        else
            authorityLen = strlen(p);

        if (
            authorityLen == 0 ||
            authorityLen >= MAX_HOST_LEN
        ) {

            Serial.println(
                "[STREAM] Invalid host length"
            );

            return false;
        }

        char authority[MAX_HOST_LEN];

        memcpy(
            authority,
            p,
            authorityLen
        );

        authority[authorityLen] = '\0';

        currentPort =
            secureConnection
                ? 443
                : 80;

        // -----------------------------------------------------
        // Host / port
        //
        // IPv4/domain:
        //     example.com:8443
        //
        // Basic IPv6 literal:
        //     [2001:db8::1]:8443
        // -----------------------------------------------------

        if (authority[0] == '[') {

            char* closing =
                strchr(authority, ']');

            if (closing == nullptr) {

                Serial.println(
                    "[STREAM] Invalid IPv6 host"
                );

                return false;
            }

            size_t hostLen =
                closing - authority - 1;

            if (hostLen >= MAX_HOST_LEN)
                return false;

            memcpy(
                currentHost,
                authority + 1,
                hostLen
            );

            currentHost[hostLen] = '\0';

            if (closing[1] == ':') {

                int port =
                    atoi(closing + 2);

                if (
                    port <= 0 ||
                    port > 65535
                ) {
                    return false;
                }

                currentPort =
                    (uint16_t)port;
            }

        } else {

            char* colon =
                strrchr(authority, ':');

            bool numericPort = false;

            if (
                colon != nullptr &&
                colon[1] != '\0'
            ) {

                numericPort = true;

                for (
                    char* q = colon + 1;
                    *q != '\0';
                    q++
                ) {

                    if (
                        !isdigit(
                            (unsigned char)*q
                        )
                    ) {
                        numericPort = false;
                        break;
                    }
                }
            }

            if (numericPort) {

                int port =
                    atoi(colon + 1);

                if (
                    port <= 0 ||
                    port > 65535
                ) {
                    return false;
                }

                currentPort =
                    (uint16_t)port;

                *colon = '\0';
            }

            if (
                authority[0] == '\0' ||
                strlen(authority) >= MAX_HOST_LEN
            ) {
                return false;
            }

            safeCopy(
                currentHost,
                sizeof(currentHost),
                authority
            );
        }

        // -----------------------------------------------------
        // Path + query
        // -----------------------------------------------------

        if (authorityEnd == nullptr) {

            safeCopy(
                currentPath,
                sizeof(currentPath),
                "/"
            );

        } else {

            const char* pathStart =
                authorityEnd;

            // Do not send URL fragment (#...) to server.
            size_t pathLen =
                strcspn(pathStart, "#");

            if (
                pathLen == 0 ||
                pathLen >= MAX_PATH_LEN
            ) {

                return false;
            }

            // "?token=..." -> "/?token=..."
            if (pathStart[0] == '?') {

                int written = snprintf(
                    currentPath,
                    sizeof(currentPath),
                    "/%.*s",
                    (int)pathLen,
                    pathStart
                );

                if (
                    written < 0 ||
                    (size_t)written >= sizeof(currentPath)
                ) {
                    return false;
                }

            } else {

                if (
                    pathStart[0] != '/'
                ) {
                    return false;
                }

                if (
                    pathLen >= sizeof(currentPath)
                ) {
                    return false;
                }

                memcpy(
                    currentPath,
                    pathStart,
                    pathLen
                );

                currentPath[pathLen] = '\0';
            }
        }

        Serial.printf(
            "[STREAM] Parsed URL: %s://%s:%u%s\n",

            secureConnection
                ? "https"
                : "http",

            currentHost,
            currentPort,
            currentPath
        );

        return true;
    }


    // =========================================================
    // OPEN TCP / TLS
    // =========================================================

    static bool openConnection() {

        activeClient = nullptr;

        httpClient.stop();
        httpsClient.stop();

        vTaskDelay(
            pdMS_TO_TICKS(100)
        );

        // -----------------------------------------------------
        // HTTPS
        // -----------------------------------------------------

        if (secureConnection) {

            Serial.printf(
                "[STREAM] TLS connect: %s:%u\n",
                currentHost,
                currentPort
            );

            // Diagnostic/fallback mode:
            // TLS is encrypted, but the certificate is not
            // validated. This is intentional for compatibility
            // with embedded radio firmware.
            httpsClient.setInsecure();

            httpsClient.setTimeout(
                CONNECT_TIMEOUT
            );

            for (
                int attempt = 1;
                attempt <= 3;
                attempt++
            ) {

                Serial.printf(
                    "[STREAM] HTTPS attempt %d/3\n",
                    attempt
                );

                // Hostname is used intentionally for TLS/SNI.
                if (
                    httpsClient.connect(
                        currentHost,
                        currentPort
                    )
                ) {

                    activeClient =
                        &httpsClient;

                    Serial.println(
                        "[STREAM] TLS connected"
                    );

                    return true;
                }

                Serial.println(
                    "[STREAM] HTTPS connection failed"
                );

                httpsClient.stop();

                vTaskDelay(
                    pdMS_TO_TICKS(500)
                );
            }

            return false;
        }

        // -----------------------------------------------------
        // HTTP
        // -----------------------------------------------------

        Serial.printf(
            "[STREAM] TCP connect: %s:%u\n",
            currentHost,
            currentPort
        );

        httpClient.setTimeout(
            CONNECT_TIMEOUT
        );

        for (
            int attempt = 1;
            attempt <= 3;
            attempt++
        ) {

            Serial.printf(
                "[STREAM] HTTP attempt %d/3\n",
                attempt
            );

            if (
                httpClient.connect(
                    currentHost,
                    currentPort
                )
            ) {

                activeClient =
                    &httpClient;

                Serial.println(
                    "[STREAM] TCP connected"
                );

                return true;
            }

            Serial.println(
                "[STREAM] TCP connection failed"
            );

            httpClient.stop();

            vTaskDelay(
                pdMS_TO_TICKS(500)
            );
        }

        return false;
    }


    // =========================================================
    // SEND HTTP REQUEST
    // =========================================================

    static void sendHttpRequest() {

        if (activeClient == nullptr)
            return;

        Serial.printf(
            "[STREAM] GET %s\n",
            currentPath
        );

        activeClient->printf(
            "GET %s HTTP/1.1\r\n"
            "Host: ",
            currentPath
        );

        // Host header:
        // IPv6 needs brackets.
        if (
            strchr(currentHost, ':') != nullptr
        ) {

            activeClient->printf(
                "[%s]",
                currentHost
            );

        } else {

            activeClient->print(
                currentHost
            );
        }

        if (
            (!secureConnection && currentPort != 80) ||
            (secureConnection && currentPort != 443)
        ) {

            activeClient->printf(
                ":%u",
                currentPort
            );
        }

        activeClient->print(
            "\r\n"
            "User-Agent: ESP32-Radio/1.0\r\n"
            "Accept: audio/mpeg, audio/aac, audio/ogg, audio/*\r\n"
            "Icy-MetaData: 0\r\n"
            "Accept-Encoding: identity\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
        );
    }


    // =========================================================
    // READ HTTP RESPONSE HEADERS
    // =========================================================

    static int readHttpHeaders() {

        if (activeClient == nullptr)
            return -1;

        responseHeaders[0] = '\0';
        redirectLocation[0] = '\0';

        chunkedTransfer = false;
        icyStream = false;
        chunkRemaining = 0;
        chunkFinished = false;

        size_t len = 0;

        unsigned long start =
            millis();

        while (
            activeClient->connected()
        ) {

            if (activeClient->available()) {

                char c =
                    (char)activeClient->read();

                if (
                    len >= MAX_HEADER_LEN - 1
                ) {

                    Serial.println(
                        "[STREAM] HTTP headers too long"
                    );

                    stopConnection();

                    return -1;
                }

                responseHeaders[len++] = c;
                responseHeaders[len] = '\0';

                // CRLF CRLF
                if (
                    len >= 4 &&
                    responseHeaders[len - 4] == '\r' &&
                    responseHeaders[len - 3] == '\n' &&
                    responseHeaders[len - 2] == '\r' &&
                    responseHeaders[len - 1] == '\n'
                ) {
                    break;
                }

                // LF LF
                if (
                    len >= 2 &&
                    responseHeaders[len - 2] == '\n' &&
                    responseHeaders[len - 1] == '\n'
                ) {
                    break;
                }
            }

            if (
                millis() - start >
                HEADER_TIMEOUT
            ) {

                Serial.println(
                    "[STREAM] HTTP header timeout"
                );

                stopConnection();

                return -1;
            }

            vTaskDelay(
                pdMS_TO_TICKS(1)
            );
        }

        if (len == 0) {

            Serial.println(
                "[STREAM] Empty HTTP response"
            );

            stopConnection();

            return -1;
        }

        // -----------------------------------------------------
        // Diagnostics
        // -----------------------------------------------------

        Serial.println(
            "[STREAM] -------- HTTP RESPONSE --------"
        );

        printResponseHeaders(
            responseHeaders
        );

        Serial.println(
            "[STREAM] --------------------------------"
        );

        // -----------------------------------------------------
        // Status
        // -----------------------------------------------------

        int status =
            parseStatusCode(
                responseHeaders
            );

        if (status < 0) {

            Serial.println(
                "[STREAM] Invalid HTTP/ICY response"
            );

            stopConnection();

            return -1;
        }

        // -----------------------------------------------------
        // Redirect
        // -----------------------------------------------------

        getHeaderValue(
            responseHeaders,
            "Location:",
            redirectLocation,
            sizeof(redirectLocation)
        );

        // -----------------------------------------------------
        // Transfer-Encoding
        // -----------------------------------------------------

        chunkedTransfer =
            containsHeaderValue(
                responseHeaders,
                "Transfer-Encoding:",
                "chunked"
            );

        if (chunkedTransfer) {

            Serial.println(
                "[STREAM] Transfer-Encoding: chunked"
            );
        }

        // -----------------------------------------------------
        // ICY
        // -----------------------------------------------------

        icyStream =
            startsWith(
                responseHeaders,
                "ICY "
            );

        // -----------------------------------------------------
        // Useful diagnostics
        // -----------------------------------------------------

        char contentType[128];

        if (
            getHeaderValue(
                responseHeaders,
                "Content-Type:",
                contentType,
                sizeof(contentType)
            )
        ) {

            Serial.printf(
                "[STREAM] Content-Type: %s\n",
                contentType
            );
        }

        char server[128];

        if (
            getHeaderValue(
                responseHeaders,
                "Server:",
                server,
                sizeof(server)
            )
        ) {

            Serial.printf(
                "[STREAM] Server: %s\n",
                server
            );
        }

        if (
            redirectLocation[0] != '\0'
        ) {

            Serial.printf(
                "[STREAM] Location: %s\n",
                redirectLocation
            );
        }

        return status;
    }


    // =========================================================
    // HTTP STATUS
    // =========================================================

    static int parseStatusCode(
        const char* headers
    ) {

        if (headers == nullptr)
            return -1;

        // HTTP/1.0 200 ...
        // HTTP/1.1 200 ...

        if (
            startsWith(headers, "HTTP/")
        ) {

            const char* space =
                strchr(headers, ' ');

            if (space == nullptr)
                return -1;

            int code =
                atoi(space + 1);

            if (
                code < 100 ||
                code > 599
            ) {
                return -1;
            }

            return code;
        }

        // ICY 200 OK

        if (
            startsWith(headers, "ICY ")
        ) {

            int code =
                atoi(headers + 4);

            if (
                code < 100 ||
                code > 599
            ) {
                return -1;
            }

            return code;
        }

        return -1;
    }


    // =========================================================
    // REDIRECT RESOLUTION
    // =========================================================

    static bool resolveRedirect(
        const char* location,
        char* out,
        size_t outSize
    ) {

        if (
            location == nullptr ||
            location[0] == '\0'
        ) {
            return false;
        }

        // Trim leading whitespace.
        while (
            *location == ' ' ||
            *location == '\t'
        ) {
            location++;
        }

        // Absolute URL.
        if (
            startsWith(location, "http://") ||
            startsWith(location, "https://")
        ) {

            return safeCopy(
                out,
                outSize,
                location
            );
        }

        // Protocol-relative:
        // //example.com/stream

        if (
            location[0] == '/' &&
            location[1] == '/'
        ) {

            int written = snprintf(
                out,
                outSize,
                "%s:%s",
                secureConnection
                    ? "https"
                    : "http",
                location
            );

            return written >= 0 &&
                   (size_t)written < outSize;
        }

        // Root-relative:
        // /stream

        if (
            location[0] == '/'
        ) {

            int written = snprintf(
                out,
                outSize,
                "%s://%s:%u%s",
                secureConnection
                    ? "https"
                    : "http",
                currentHost,
                currentPort,
                location
            );

            return written >= 0 &&
                   (size_t)written < outSize;
        }

        // Relative path:
        // stream2.mp3

        const char* slash =
            strrchr(currentPath, '/');

        if (slash == nullptr)
            return false;

        size_t baseLen =
            (size_t)(slash - currentPath + 1);

        if (baseLen >= MAX_PATH_LEN)
            return false;

        char basePath[MAX_PATH_LEN];

        memcpy(
            basePath,
            currentPath,
            baseLen
        );

        basePath[baseLen] = '\0';

        int written = snprintf(
            out,
            outSize,
            "%s://%s:%u%s%s",
            secureConnection
                ? "https"
                : "http",
            currentHost,
            currentPort,
            basePath,
            location
        );

        return written >= 0 &&
               (size_t)written < outSize;
    }


    // =========================================================
    // STREAM DATA
    // =========================================================

    static int readStreamData(
        int maxBytes
    ) {

        if (
            activeClient == nullptr ||
            maxBytes <= 0
        ) {
            return 0;
        }

        // -----------------------------------------------------
        // Normal streaming response
        // -----------------------------------------------------

        if (!chunkedTransfer) {

            int available =
                activeClient->available();

            if (available <= 0)
                return 0;

            int toRead =
                min(
                    available,
                    min(
                        maxBytes,
                        (int)sizeof(chunkBuffer)
                    )
                );

            int n =
                activeClient->read(
                    chunkBuffer,
                    toRead
                );

            if (n > 0) {

                fifoPush(
                    chunkBuffer,
                    n
                );
            }

            return n;
        }

        // -----------------------------------------------------
        // Chunked transfer
        // -----------------------------------------------------

        int total = 0;

        while (
            total < maxBytes
        ) {

            if (chunkFinished)
                break;

            if (chunkRemaining == 0) {

                long chunkSize =
                    readChunkSize();

                if (chunkSize < 0)
                    break;

                if (chunkSize == 0) {

                    chunkFinished = true;

                    consumeChunkTrailer();

                    break;
                }

                chunkRemaining =
                    (size_t)chunkSize;
            }

            int available =
                activeClient->available();

            if (available <= 0)
                break;

            int toRead =
                min(
                    available,
                    min(
                        maxBytes - total,
                        (int)sizeof(chunkBuffer)
                    )
                );

            toRead =
                min(
                    toRead,
                    (int)chunkRemaining
                );

            if (toRead <= 0)
                break;

            int n =
                activeClient->read(
                    chunkBuffer,
                    toRead
                );

            if (n <= 0)
                break;

            fifoPush(
                chunkBuffer,
                n
            );

            total += n;
            chunkRemaining -= n;

            if (chunkRemaining == 0) {
                consumeCRLF();
            }
        }

        return total;
    }


    // =========================================================
    // CHUNK SIZE
    // =========================================================

    static long readChunkSize() {

        char line[32];

        int len = 0;

        unsigned long start =
            millis();

        while (
            millis() - start < 3000UL
        ) {

            if (activeClient->available()) {

                char c =
                    (char)activeClient->read();

                if (c == '\n') {

                    line[len] = '\0';

                    while (
                        len > 0 &&
                        line[len - 1] == '\r'
                    ) {
                        line[--len] = '\0';
                    }

                    // Remove chunk extensions.
                    char* semicolon =
                        strchr(line, ';');

                    if (semicolon != nullptr)
                        *semicolon = '\0';

                    if (len == 0)
                        return -1;

                    char* endPtr = nullptr;

                    long size =
                        strtol(
                            line,
                            &endPtr,
                            16
                        );

                    if (
                        endPtr == line ||
                        size < 0
                    ) {
                        Serial.println(
                            "[STREAM] Invalid chunk size"
                        );

                        return -1;
                    }

                    return size;
                }

                if (
                    len <
                    (int)sizeof(line) - 1
                ) {
                    line[len++] = c;
                }

            } else {

                vTaskDelay(
                    pdMS_TO_TICKS(1)
                );
            }
        }

        Serial.println(
            "[STREAM] Chunk size timeout"
        );

        return -1;
    }


    // =========================================================
    // CONSUME CRLF AFTER CHUNK
    // =========================================================

    static void consumeCRLF() {

        int count = 0;

        unsigned long start =
            millis();

        while (
            count < 2 &&
            millis() - start < 1000UL
        ) {

            if (activeClient->available()) {

                activeClient->read();
                count++;

            } else {

                vTaskDelay(
                    pdMS_TO_TICKS(1)
                );
            }
        }
    }


    // =========================================================
    // CONSUME CHUNK TRAILER
    // =========================================================

    static void consumeChunkTrailer() {

        char previous = 0;

        unsigned long start =
            millis();

        while (
            millis() - start < 3000UL
        ) {

            if (activeClient->available()) {

                char current =
                    (char)activeClient->read();

                if (
                    previous == '\r' &&
                    current == '\n'
                ) {
                    break;
                }

                previous = current;

            } else {

                vTaskDelay(
                    pdMS_TO_TICKS(1)
                );
            }
        }
    }


    // =========================================================
    // STOP CONNECTION
    // =========================================================

    static void stopConnection() {

        if (httpClient.connected())
            httpClient.stop();

        if (httpsClient.connected())
            httpsClient.stop();

        activeClient = nullptr;
    }


    // =========================================================
    // HEADER VALUE
    // =========================================================

    static bool getHeaderValue(
        const char* headers,
        const char* headerName,
        char* out,
        size_t outSize
    ) {

        if (
            headers == nullptr ||
            headerName == nullptr ||
            out == nullptr ||
            outSize == 0
        ) {
            return false;
        }

        const char* p = headers;

        while (*p != '\0') {

            const char* lineEnd =
                strchr(p, '\n');

            if (lineEnd == nullptr)
                lineEnd = p + strlen(p);

            if (
                startsWithIgnoreCase(
                    p,
                    headerName
                )
            ) {

                const char* colon =
                    strchr(p, ':');

                if (
                    colon != nullptr &&
                    colon < lineEnd
                ) {

                    const char* valueStart =
                        colon + 1;

                    while (
                        valueStart < lineEnd &&
                        (
                            *valueStart == ' ' ||
                            *valueStart == '\t'
                        )
                    ) {
                        valueStart++;
                    }

                    size_t len =
                        (size_t)(lineEnd - valueStart);

                    while (
                        len > 0 &&
                        (
                            valueStart[len - 1] == '\r' ||
                            valueStart[len - 1] == ' ' ||
                            valueStart[len - 1] == '\t'
                        )
                    ) {
                        len--;
                    }

                    if (len >= outSize)
                        len = outSize - 1;

                    memcpy(
                        out,
                        valueStart,
                        len
                    );

                    out[len] = '\0';

                    return true;
                }
            }

            if (*lineEnd == '\0')
                break;

            p = lineEnd + 1;
        }

        return false;
    }


    // =========================================================
    // HEADER CONTAINS VALUE
    // =========================================================

    static bool containsHeaderValue(
        const char* headers,
        const char* headerName,
        const char* value
    ) {

        char headerValue[128];

        if (
            !getHeaderValue(
                headers,
                headerName,
                headerValue,
                sizeof(headerValue)
            )
        ) {
            return false;
        }

        return containsIgnoreCase(
            headerValue,
            strlen(headerValue),
            value
        );
    }


    // =========================================================
    // PRINT RESPONSE HEADERS
    // =========================================================

    static void printResponseHeaders(
        const char* headers
    ) {

        const char* p = headers;

        int lines = 0;

        while (
            *p != '\0' &&
            lines < 40
        ) {

            const char* end =
                strchr(p, '\n');

            if (end == nullptr) {

                Serial.println(p);
                break;
            }

            size_t len =
                (size_t)(end - p);

            char line[256];

            if (
                len >= sizeof(line)
            ) {
                len = sizeof(line) - 1;
            }

            memcpy(
                line,
                p,
                len
            );

            line[len] = '\0';

            if (
                len > 0 &&
                line[len - 1] == '\r'
            ) {
                line[len - 1] = '\0';
            }

            Serial.println(line);

            p = end + 1;
            lines++;
        }
    }


    // =========================================================
    // STRING HELPERS
    // =========================================================

    static bool startsWith(
        const char* str,
        const char* prefix
    ) {

        if (
            str == nullptr ||
            prefix == nullptr
        ) {
            return false;
        }

        return strncmp(
            str,
            prefix,
            strlen(prefix)
        ) == 0;
    }


    static bool startsWithIgnoreCase(
        const char* str,
        const char* prefix
    ) {

        while (*prefix != '\0') {

            if (*str == '\0')
                return false;

            if (
                toLowerChar(*str) !=
                toLowerChar(*prefix)
            ) {
                return false;
            }

            str++;
            prefix++;
        }

        return true;
    }


    static bool containsIgnoreCase(
        const char* str,
        size_t len,
        const char* value
    ) {

        size_t valueLen =
            strlen(value);

        if (
            valueLen == 0 ||
            len < valueLen
        ) {
            return false;
        }

        for (
            size_t i = 0;
            i <= len - valueLen;
            i++
        ) {

            bool match = true;

            for (
                size_t j = 0;
                j < valueLen;
                j++
            ) {

                if (
                    toLowerChar(str[i + j]) !=
                    toLowerChar(value[j])
                ) {

                    match = false;
                    break;
                }
            }

            if (match)
                return true;
        }

        return false;
    }


    static char toLowerChar(
        char c
    ) {

        if (
            c >= 'A' &&
            c <= 'Z'
        ) {
            return c + ('a' - 'A');
        }

        return c;
    }


    static bool safeCopy(
        char* dst,
        size_t dstSize,
        const char* src
    ) {

        if (
            dst == nullptr ||
            src == nullptr ||
            dstSize == 0
        ) {
            return false;
        }

        size_t len =
            strlen(src);

        if (len >= dstSize) {

            dst[0] = '\0';

            return false;
        }

        memcpy(
            dst,
            src,
            len + 1
        );

        return true;
    }


    // =========================================================
    // FIFO RESET
    // =========================================================

    static void resetFifo() {

        portENTER_CRITICAL(&fifoMux);

        fifoHead = 0;
        fifoTail = 0;

        portEXIT_CRITICAL(&fifoMux);
    }


    // =========================================================
    // FIFO PUSH
    // =========================================================

    static void fifoPush(
        const uint8_t* data,
        int len
    ) {

        if (
            data == nullptr ||
            len <= 0
        ) {
            return;
        }

        portENTER_CRITICAL(&fifoMux);

        for (
            int i = 0;
            i < len;
            i++
        ) {

            int nextHead =
                (fifoHead + 1) %
                FIFO_SIZE;

            if (
                nextHead ==
                fifoTail
            ) {
                break;
            }

            fifo[fifoHead] =
                data[i];

            fifoHead =
                nextHead;
        }

        portEXIT_CRITICAL(&fifoMux);
    }


    // =========================================================
    // FIFO POP
    // =========================================================

    static int fifoPop(
        uint8_t* out,
        int maxLen
    ) {

        if (
            out == nullptr ||
            maxLen <= 0
        ) {
            return 0;
        }

        int count = 0;

        portENTER_CRITICAL(&fifoMux);

        while (
            count < maxLen &&
            fifoTail != fifoHead
        ) {

            out[count++] =
                fifo[fifoTail];

            fifoTail =
                (fifoTail + 1) %
                FIFO_SIZE;
        }

        portEXIT_CRITICAL(&fifoMux);

        return count;
    }
};


// =============================================================
// STATIC VARIABLES
// =============================================================

WiFiClient
RadioStream::httpClient;

WiFiClientSecure
RadioStream::httpsClient;

Client*
RadioStream::activeClient = nullptr;

bool
RadioStream::secureConnection = false;

char
RadioStream::currentHost[
    RadioStream::MAX_HOST_LEN
] = {0};

char
RadioStream::currentPath[
    RadioStream::MAX_PATH_LEN
] = {0};

uint16_t
RadioStream::currentPort = 80;

int
RadioStream::currentStatusCode = 0;

bool
RadioStream::chunkedTransfer = false;

bool
RadioStream::icyStream = false;

char
RadioStream::responseHeaders[
    RadioStream::MAX_HEADER_LEN
] = {0};

char
RadioStream::redirectLocation[
    RadioStream::MAX_URL_LEN
] = {0};

uint8_t
RadioStream::fifo[
    RadioStream::FIFO_SIZE
];

volatile int
RadioStream::fifoHead = 0;

volatile int
RadioStream::fifoTail = 0;

portMUX_TYPE
RadioStream::fifoMux =
    portMUX_INITIALIZER_UNLOCKED;

unsigned long
RadioStream::lastDataTime = 0;

volatile bool
RadioStream::streamActive = false;

volatile bool
RadioStream::isConnecting = false;

unsigned long
RadioStream::lastDebugPrint = 0;

int
RadioStream::reconnectAttempts = 0;

size_t
RadioStream::chunkRemaining = 0;

bool
RadioStream::chunkFinished = false;

uint8_t
RadioStream::chunkBuffer[
    512
];


#endif
