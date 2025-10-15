/*
 * EVCC Display Configuration
 * Hardware, display, timing, and color configurations
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <lvgl.h>

// Pin definitions for Freenove ESP32 Display
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   27

// Display dimensions
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320
#define UPPER_SECTION_HEIGHT (SCREEN_HEIGHT / 2)
#define LOWER_SECTION_HEIGHT (SCREEN_HEIGHT - UPPER_SECTION_HEIGHT)
#define PADDING 4
#define COLUMN_WIDTH ((SCREEN_WIDTH - (3 * PADDING)) / 2)

// Timing configuration
#define POLL_INTERVAL 10000     // 10 seconds
#define HTTP_TIMEOUT 8000       // 8 seconds
#define ROTATION_INTERVAL 10000 // 10 seconds for loadpoint rotation

// Threshold below which a power flow is considered inactive (used for dimming text)
#ifndef POWER_ACTIVE_THRESHOLD
#define POWER_ACTIVE_THRESHOLD 10.0f
#endif

// Debug configuration
#define DEBUG_MODE false        // Enable debug logging (Serial + Web) (default off)
#define WEB_SERVER_PORT 80      // HTTP server port for status/logs
#define LOG_BUFFER_SIZE 100     // Maximum number of log entries to keep

// Logging levels
#define LOG_LEVEL_ERROR   ((uint8_t)0)
#define LOG_LEVEL_WARN    ((uint8_t)1)
#define LOG_LEVEL_INFO    ((uint8_t)2)
#define LOG_LEVEL_DEBUG   ((uint8_t)3)
#define LOG_LEVEL_VERBOSE ((uint8_t)4)

// Minimum level to store / display (compile time). Can be overridden later.
#ifndef LOG_MIN_LEVEL
#define LOG_MIN_LEVEL LOG_LEVEL_INFO
#endif

// Color definitions
#define COLOR_GRID_BG     0xf3f3f7
#define COLOR_PANEL_BG    0xFFFFFF
#define COLOR_PANEL_BORDER 0xe0e0e0
#define COLOR_TEXT_PRIMARY 0x000000
#define COLOR_TEXT_SECONDARY 0x93949e
#define COLOR_TEXT_VALUE  0x333333
#define COLOR_PULSE_BORDER 0xFF9800  // Orange charging animation border

// Bar diagram colors
#define COLOR_BAR_GENERATION  0x4CAF50   // Green for PV generation
#define COLOR_BAR_BATTERY_OUT 0xFF9800   // Orange for battery discharge
#define COLOR_BAR_GRID_IN     0xF44336   // Red for grid consumption
#define COLOR_BAR_CONSUMPTION 0x2196F3   // Blue for house consumption
#define COLOR_BAR_LOADPOINT   0x9C27B0   // Purple for car charging
#define COLOR_BAR_BATTERY_IN  0xFFEB3B   // Yellow for battery charging
#define COLOR_BAR_GRID_OUT    0x00BCD4   // Cyan for grid feed-in
#define COLOR_BAR_BACKGROUND  0xE0E0E0   // Light gray for bar background

// Style configuration constants
#define FONT_PRIMARY &lv_font_montserrat_16
#define FONT_SECONDARY &lv_font_montserrat_14
#define FONT_SMALL &lv_font_montserrat_12
#define CONTAINER_PAD 4
#define CONTAINER_RADIUS 0

// EVCC API endpoint path  
#define EVCC_API_PATH "/api/state?jq={gridPower:.grid.power,pvPower:.pvPower,batterySoc:.batterySoc,homePower:.homePower,batteryPower:.batteryPower,solar:{scale:(.forecast.solar.scale),todayEnergy:(.forecast.solar.today.energy)},loadpoints:[.loadpoints[0],.loadpoints[1]]|map(select(.!=null)|{chargePower:.chargePower,soc:(.vehicleSoc//.soc),charging:.charging,plugged:(.connected//.plugged),title:.title,vehicletitle:.vehicleTitle,vehicleRange:.vehicleRange,effectivePlanTime:.effectivePlanTime,effectivePlanSoc:.effectivePlanSoc,effectiveLimitSoc:.effectiveLimitSoc,planProjectedStart:.planProjectedStart,chargeCurrents:.chargeCurrents,maxCurrent:.maxCurrent,offeredCurrent:.chargeCurrent,phasesActive:.phasesActive})}"

// Data structure for EVCC loadpoint values
struct LoadpointData {
    float soc = -1.0;
    float chargePower = 0.0;
    String title;
    String vehicleTitle;
    bool charging = false;
    bool plugged = false;
    float vehicleRange = -1.0;
    String effectivePlanTime;
    float effectivePlanSoc = -1.0;
    float effectiveLimitSoc = -1.0;
    String planProjectedStart;
    float chargeCurrents[3] = {0.0, 0.0, 0.0}; // Current per phase
    float maxCurrent = 0.0;
    float offeredCurrent = 0.0;
    int phasesActive = 0;
    
    // Constructor with string reservation
    LoadpointData() {
        title.reserve(16);
        vehicleTitle.reserve(32);
        effectivePlanTime.reserve(32);
        planProjectedStart.reserve(32);
    }
};

// Data structure for all EVCC values
struct EVCCData {
    float gridPower = 0.0;
    float pvPower = 0.0;
    float batterySoc = -1.0;
    float homePower = 0.0;
    float batteryPower = 0.0;
    
    // Solar forecast data
    float solarForecastScale = 1.0;
    float solarForecastTodayEnergy = 0.0;
    
    // Loadpoint data
    LoadpointData lp1, lp2;
    
    unsigned long lastUpdate = 0;
    int consecutiveFailures = 0;
};

// Loadpoint rotation state structure
struct RotationState {
    bool currentLoadpoint = true; // true = LP1, false = LP2
    unsigned long lastRotation = 0;
};

#endif // CONFIG_H