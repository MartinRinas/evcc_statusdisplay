/*
 * Logging system for EVCC Display
 * Fixed-size ring buffer with log levels and thread-safe access
 */

#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include "config.h"

// Log buffer structure (fixed-size ring buffer)
struct LogEntry {
    unsigned long timestamp; // ms since boot
    time_t epoch;            // epoch seconds (0 if not synced yet)
    uint8_t level;           // log level
    char message[96];        // fixed-size message buffer
};

// Global log state
extern LogEntry logBuffer[LOG_BUFFER_SIZE];
extern int logHead;
extern int logCount;
extern bool debugEnabled;
extern uint32_t logTotal;
extern uint32_t logOverwrites;
extern uint32_t logDropped;
extern portMUX_TYPE logMux;

// Convert level to short string
inline const char* levelToStr(uint8_t lvl) {
    switch(lvl) {
        case LOG_LEVEL_ERROR: return "ERR";
        case LOG_LEVEL_WARN: return  "WRN";
        case LOG_LEVEL_INFO: return  "INF";
        case LOG_LEVEL_DEBUG: return "DBG";
        case LOG_LEVEL_VERBOSE: return "VRB";
        default: return "UNK";
    }
}

// Core logging function
inline void logMessage(uint8_t level, const String& msg, bool forceSerial = false) {
    if (level > LOG_LEVEL_VERBOSE) level = LOG_LEVEL_VERBOSE; // clamp
    if (level < LOG_MIN_LEVEL) {
        // Considered dropped for display purposes
        logDropped++;
        return;
    }
    unsigned long nowMs = millis();
    time_t nowEpoch = time(nullptr);

    portENTER_CRITICAL(&logMux);
    LogEntry &slot = logBuffer[logHead];
    slot.timestamp = nowMs;
    slot.epoch = nowEpoch;
    slot.level = level;
    // Copy message into fixed buffer
    size_t len = msg.length();
    if (len > sizeof(slot.message) - 1) len = sizeof(slot.message) - 1;
    memcpy(slot.message, msg.c_str(), len);
    slot.message[len] = '\0';
    logHead = (logHead + 1) % LOG_BUFFER_SIZE;
    if (logCount < LOG_BUFFER_SIZE) {
        logCount++;
    } else {
        logOverwrites++;
    }
    logTotal++;
    portEXIT_CRITICAL(&logMux);

    if (debugEnabled || forceSerial) {
        Serial.println(msg);
    }
}

// Backward-compatible overload (defaults to INFO)
inline void logMessage(const String& message, bool forceSerial = false) {
    logMessage((uint8_t)LOG_LEVEL_INFO, message, forceSerial);
}

#endif // LOGGING_H
