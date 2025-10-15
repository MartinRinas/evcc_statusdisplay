/*
 * EVCC Display for ESP32 with ST7796 TFT
 * C++ version with LVGL, WiFi, and HTTP polling
 * Replaces MicroPython version for better memory management
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <esp_task_wdt.h>
#include "wifi_config.h"

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

// WiFi credentials - see wifi_config.h
// Note: Copy wifi_config.h.template to wifi_config.h and configure
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// EVCC server configuration - see wifi_config.h
const char* evcc_host = EVCC_HOST;
const int evcc_port = EVCC_PORT;
const char* combined_path = "/api/state?jq={gridPower:.grid.power,pvPower:.pvPower,batterySoc:.batterySoc,homePower:.homePower,batteryPower:.batteryPower,loadpoints:[.loadpoints[0],.loadpoints[1]]|map(select(.!=null)|{chargePower:.chargePower,soc:(.vehicleSoc//.soc),charging:.charging,plugged:(.connected//.plugged),title:.title,vehicletitle:.vehicleTitle,vehicleRange:.vehicleRange,effectivePlanTime:.effectivePlanTime,effectivePlanSoc:.effectivePlanSoc,effectiveLimitSoc:.effectiveLimitSoc,planProjectedStart:.planProjectedStart})}";

// Timing configuration
const unsigned long POLL_INTERVAL = 10000; // 10 seconds
const unsigned long HTTP_TIMEOUT = 8000;   // 8 seconds

// Color definitions
#define COLOR_GRID_BG     0xf3f3f7
#define COLOR_PANEL_BG    0xFFFFFF
#define COLOR_PANEL_BORDER 0xe0e0e0
#define COLOR_TEXT_PRIMARY 0x000000
#define COLOR_TEXT_SECONDARY 0x93949e
#define COLOR_TEXT_VALUE  0x333333
#define COLOR_PULSE_BORDER 0xFF9800  // Orange charging animation border

// Style configuration constants
#define FONT_PRIMARY &lv_font_montserrat_16
#define FONT_SECONDARY &lv_font_montserrat_14
#define FONT_SMALL &lv_font_montserrat_12
#define CONTAINER_PAD 4
#define CONTAINER_RADIUS 0

// Display and LVGL setup
TFT_eSPI tft = TFT_eSPI();
static lv_color_t buf[SCREEN_WIDTH * 10];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

// Data structure for EVCC values
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
    
    // Constructor with string reservation
    LoadpointData() {
        title.reserve(16);
        vehicleTitle.reserve(32);
        effectivePlanTime.reserve(32);
        planProjectedStart.reserve(32);
    }
};

struct EVCCData {
    float gridPower = 0.0;
    float pvPower = 0.0;
    float batterySoc = -1.0;
    float homePower = 0.0;
    float batteryPower = 0.0;
    
    // Loadpoint data
    LoadpointData lp1, lp2;
    
    unsigned long lastUpdate = 0;
    int consecutiveFailures = 0;
};

EVCCData data;

// Pulsing border animation
lv_style_t pulse_style;
lv_timer_t* pulse_timer = nullptr;
int pulse_direction = 1; // 1 for increasing, -1 for decreasing
int pulse_opacity = 50;  // Current opacity (0-255)

// Loadpoint rotation state
struct {
    bool currentLoadpoint = true; // true = LP1, false = LP2
    unsigned long lastRotation = 0;
    const unsigned long ROTATION_INTERVAL = 10000; // 10 seconds
} rotationState;

// UI element references
struct UIElements {
    lv_obj_t* screen;
    lv_obj_t* upper_container;
    lv_obj_t* lower_container;
    
    // Energy row labels (value1, value2)
    struct {
        lv_obj_t* desc;
        lv_obj_t* value1;
        lv_obj_t* value2;
    } generation, battery_discharge, grid_feed, consumption, loadpoint, battery_charge, grid_feedin;
    
    // Car section elements
    struct {
        lv_obj_t* title_label;
        lv_obj_t* car_label;
        lv_obj_t* power_label;
        lv_obj_t* soc_bar;
        lv_obj_t* plan_soc_marker;
        lv_obj_t* limit_soc_marker;
        lv_obj_t* soc_desc;
        lv_obj_t* plan_desc;
        lv_obj_t* limit_desc;
        lv_obj_t* soc_value;
        lv_obj_t* range_value;
        lv_obj_t* ladedauer_value;
        lv_obj_t* plan_value;
        lv_obj_t* plan_soc_value;
        lv_obj_t* ladelimit_value;
    } car;
};

UIElements ui;

// Common styling functions
void styleLabel(lv_obj_t* label, const lv_font_t* font, lv_color_t color) {
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
}

void styleLabelPrimary(lv_obj_t* label) {
    styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_PRIMARY));
}

void styleLabelSecondary(lv_obj_t* label) {
    styleLabel(label, FONT_SMALL, lv_color_hex(COLOR_TEXT_SECONDARY));
}

void styleLabelValue(lv_obj_t* label) {
    styleLabel(label, FONT_SECONDARY, lv_color_hex(COLOR_TEXT_VALUE));
}

void styleLabelHeader(lv_obj_t* label) {
    styleLabel(label, FONT_PRIMARY, lv_color_hex(COLOR_TEXT_PRIMARY));
}

void styleContainer(lv_obj_t* container) {
    lv_obj_set_style_bg_color(container, lv_color_hex(COLOR_PANEL_BG), 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, CONTAINER_PAD, 0);
    lv_obj_set_scrollbar_mode(container, LV_SCROLLBAR_MODE_OFF);
}

void styleContainerWithBorder(lv_obj_t* container) {
    styleContainer(container);
    lv_obj_set_style_border_width(container, 1, 0);
    lv_obj_set_style_border_color(container, lv_color_hex(COLOR_PANEL_BORDER), 0);
}

void positionAndAlign(lv_obj_t* obj, int x, int y, int width, lv_text_align_t align) {
    lv_obj_set_pos(obj, x, y);
    if (width > 0) {
        lv_obj_set_width(obj, width);
        lv_obj_set_style_text_align(obj, align, 0);
    }
}

// LVGL display driver callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    
    lv_disp_flush_ready(disp);
}

// Get active loadpoint data based on rotation logic
LoadpointData* getActiveLoadpoint() {
    bool lp1Charging = data.lp1.charging;
    bool lp2Charging = data.lp2.charging;
    
    // If only one is charging, show that one
    if (lp1Charging && !lp2Charging) {
        return &data.lp1;
    }
    if (lp2Charging && !lp1Charging) {
        return &data.lp2;
    }
    
    // If both charging or neither charging, rotate every 10 seconds
    unsigned long now = millis();
    if (now - rotationState.lastRotation >= rotationState.ROTATION_INTERVAL) {
        rotationState.currentLoadpoint = !rotationState.currentLoadpoint;
        rotationState.lastRotation = now;
        Serial.printf("Rotating to loadpoint %d\n", rotationState.currentLoadpoint ? 1 : 2);
    }
    
    return rotationState.currentLoadpoint ? &data.lp1 : &data.lp2;
}

// Utility functions
String formatPower(float watts) {
    if (abs(watts) < 1000) {
        return String((int)watts) + "W";
    } else if (abs(watts) < 10000) {
        return String(watts/1000.0, 1) + "kW";
    } else {
        return String(watts/1000.0, 0) + "kW";
    }
}

// Pulsing border animation callback
void pulse_animation_cb(lv_timer_t* timer) {
  // Update pulse opacity
  pulse_opacity += pulse_direction * 15;
  
  // Reverse direction at boundaries
  if (pulse_opacity >= 200) {
    pulse_opacity = 200;
    pulse_direction = -1;
  } else if (pulse_opacity <= 50) {
    pulse_opacity = 50;
    pulse_direction = 1;
  }
  
  // Apply the new opacity to the border
  lv_style_set_border_opa(&pulse_style, pulse_opacity);
  lv_obj_report_style_change(&pulse_style);
}

String formatPercentage(float value) {
    return value >= 0 ? String((int)value) + "%" : "---";
}

String formatDistance(float value) {
    return value >= 0 ? String((int)value) + "km" : "-- km";
}

String formatPlanTime(const String& isoTime) {
    if (isoTime.isEmpty() || isoTime.length() < 19) {
        return "keiner";
    }
    
    // Parse ISO 8601 format: "2025-10-12T05:00:00Z"
    int year = isoTime.substring(0, 4).toInt();
    int month = isoTime.substring(5, 7).toInt();
    int day = isoTime.substring(8, 10).toInt();
    int hour = isoTime.substring(11, 13).toInt();
    int minute = isoTime.substring(14, 16).toInt();
    
    // Get current local time to determine if we're in DST
    time_t now = time(nullptr);
    struct tm* currentLocal = localtime(&now);
    bool isDST = currentLocal->tm_isdst > 0;
    
    // Convert UTC to local time (Germany: CET=UTC+1, CEST=UTC+2)
    int localHour = hour + (isDST ? 2 : 1);
    int localDay = day;
    int localMonth = month;
    int localYear = year;
    
    // Handle day overflow
    if (localHour >= 24) {
        localHour -= 24;
        localDay++;
        
        // Simple day overflow handling (good enough for near-future dates)
        int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (localYear % 4 == 0 && (localYear % 100 != 0 || localYear % 400 == 0)) {
            daysInMonth[1] = 29; // Leap year
        }
        
        if (localDay > daysInMonth[localMonth - 1]) {
            localDay = 1;
            localMonth++;
            if (localMonth > 12) {
                localMonth = 1;
                localYear++;
            }
        }
    }
    
    // Calculate days difference from today
    int todayYear = currentLocal->tm_year + 1900;
    int todayMonth = currentLocal->tm_mon + 1;
    int todayDay = currentLocal->tm_mday;
    
    // Simple day difference calculation (good for dates within a week)
    int daysDiff = 0;
    if (localYear == todayYear && localMonth == todayMonth) {
        daysDiff = localDay - todayDay;
    } else if (localYear > todayYear || (localYear == todayYear && localMonth > todayMonth)) {
        daysDiff = 7; // Force to show date format for far future
    } else {
        daysDiff = -7; // Force to show date format for past
    }
    
    // German day names
    const char* germanDays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
    
    String dayString;
    if (daysDiff == 0) {
        dayString = "Heute";
    } else if (daysDiff == 1) {
        dayString = "Morgen";
    } else if (daysDiff >= 2 && daysDiff < 7) {
        // Calculate day of week for the target date
        struct tm targetDate = {0};
        targetDate.tm_year = localYear - 1900;
        targetDate.tm_mon = localMonth - 1;
        targetDate.tm_mday = localDay;
        mktime(&targetDate); // This normalizes and calculates tm_wday
        dayString = String(germanDays[targetDate.tm_wday]);
    } else {
        // Show date for dates outside the week
        dayString = String(localDay) + "." + String(localMonth) + ".";
    }
    
    // Format time as HH:MM
    String timeString = "";
    if (localHour < 10) timeString += "0";
    timeString += String(localHour) + ":";
    if (minute < 10) timeString += "0";
    timeString += String(minute);
    
    return dayString + " " + timeString;
}

// WiFi connection
bool connectWiFi() {
    Serial.println("Connecting to WiFi...");
    updateWiFiStatus("Connecting to WiFi...", ssid, "");
    
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        
        // Update display with dots to show progress
        char progressText[32];
        snprintf(progressText, sizeof(progressText), "Connecting to WiFi%.*s", (attempts % 4) + 1, "....");
        updateWiFiStatus(progressText, ssid, "");
        
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("Connected! IP: ");
        Serial.println(WiFi.localIP());
        
        // Show success status
        updateWiFiStatus("WiFi Connected!", ssid, WiFi.localIP().toString());
        delay(1000); // Show success message briefly
        
        return true;
    } else {
        Serial.println();
        Serial.println("Failed to connect to WiFi");
        
        // Show failure status
        updateWiFiStatus("WiFi Connection Failed", ssid, "Check credentials");
        delay(2000); // Show error message longer
        
        return false;
    }
}

// HTTP request function
bool httpGet(const char* path, String& response) {
    HTTPClient http;
    String url = "http://" + String(evcc_host) + ":" + String(evcc_port) + String(path);
    
    Serial.print("Requesting: ");
    Serial.println(url);
    
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT);
    
    int httpCode = http.GET();
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        response = http.getString();
        Serial.printf("HTTP success: %d chars\n", response.length());
        Serial.println("=== HTTP Response ===");
        Serial.println(response);
        Serial.println("=== End Response ===");
        success = true;
    } else {
        Serial.printf("HTTP error: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

// Parse combined data
bool parseCombinedData(const String& json) {
    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        Serial.printf("Combined parse error: %s\n", error.c_str());
        return false;
    }
    
    // Parse energy data
    data.gridPower = doc["gridPower"] | 0.0;
    data.pvPower = doc["pvPower"] | 0.0;
    data.batterySoc = doc["batterySoc"] | -1.0;
    data.homePower = doc["homePower"] | 0.0;
    data.batteryPower = doc["batteryPower"] | 0.0;
    
    // Reset loadpoint data
    data.lp1.soc = -1.0;
    data.lp1.chargePower = 0.0;
    data.lp1.vehicleRange = -1.0;
    data.lp1.effectivePlanTime = "";
    data.lp1.effectivePlanSoc = -1.0;
    data.lp1.effectiveLimitSoc = -1.0;
    data.lp1.planProjectedStart = "";
    data.lp2.soc = -1.0;
    data.lp2.chargePower = 0.0;
    data.lp2.vehicleRange = -1.0;
    data.lp2.effectivePlanTime = "";
    data.lp2.effectivePlanSoc = -1.0;
    data.lp2.effectiveLimitSoc = -1.0;
    data.lp2.planProjectedStart = "";
    
    // Parse loadpoint data
    JsonArray loadpoints = doc["loadpoints"];
    if (loadpoints.size() > 0 && !loadpoints[0].isNull()) {
        JsonObject lp1 = loadpoints[0];
        data.lp1.soc = lp1["soc"] | -1.0;
        data.lp1.chargePower = lp1["chargePower"] | 0.0;
        data.lp1.title = lp1["title"] | "LP1";
        data.lp1.vehicleTitle = lp1["vehicletitle"] | "";
        data.lp1.charging = lp1["charging"] | false;
        data.lp1.plugged = lp1["plugged"] | false;
        data.lp1.vehicleRange = lp1["vehicleRange"] | -1.0;
        data.lp1.effectivePlanTime = lp1["effectivePlanTime"] | "";
        data.lp1.effectivePlanSoc = lp1["effectivePlanSoc"] | -1.0;
        data.lp1.effectiveLimitSoc = lp1["effectiveLimitSoc"] | -1.0;
        data.lp1.planProjectedStart = lp1["planProjectedStart"] | "";
    }
    
    if (loadpoints.size() > 1 && !loadpoints[1].isNull()) {
        JsonObject lp2 = loadpoints[1];
        data.lp2.soc = lp2["soc"] | -1.0;
        data.lp2.chargePower = lp2["chargePower"] | 0.0;
        data.lp2.title = lp2["title"] | "LP2";
        data.lp2.vehicleTitle = lp2["vehicletitle"] | "";
        data.lp2.charging = lp2["charging"] | false;
        data.lp2.plugged = lp2["plugged"] | false;
        data.lp2.vehicleRange = lp2["vehicleRange"] | -1.0;
        data.lp2.effectivePlanTime = lp2["effectivePlanTime"] | "";
        data.lp2.effectivePlanSoc = lp2["effectivePlanSoc"] | -1.0;
        data.lp2.effectiveLimitSoc = lp2["effectiveLimitSoc"] | -1.0;
        data.lp2.planProjectedStart = lp2["planProjectedStart"] | "";
    }
    
    // Calculate derived values
    float total_charge_power = data.lp1.chargePower + data.lp2.chargePower;
    
    
    return true;
}


// Create energy row
void createEnergyRow(lv_obj_t* parent, const char* description, const char* value1, const char* value2, 
                     int y_pos, lv_obj_t** desc_out, lv_obj_t** val1_out, lv_obj_t** val2_out) {
    // Description label
    *desc_out = lv_label_create(parent);
    lv_label_set_text(*desc_out, description);
    styleLabelPrimary(*desc_out);
    lv_obj_set_pos(*desc_out, 0, y_pos);
    
    // Value 1 (left value)
    *val1_out = lv_label_create(parent);
    lv_label_set_text(*val1_out, value1);
    styleLabelValue(*val1_out);
    positionAndAlign(*val1_out, 110, y_pos, 65, LV_TEXT_ALIGN_RIGHT);
    
    // Value 2 (right value)
    *val2_out = lv_label_create(parent);
    lv_label_set_text(*val2_out, value2);
    styleLabelValue(*val2_out);
    positionAndAlign(*val2_out, 161, y_pos, 60, LV_TEXT_ALIGN_RIGHT);
}

// Create column
lv_obj_t* createColumn(lv_obj_t* parent, const char* title, int x_pos) {
    lv_obj_t* column = lv_obj_create(parent);
    lv_obj_set_pos(column, x_pos, 0);
    lv_obj_set_size(column, COLUMN_WIDTH, UPPER_SECTION_HEIGHT - (2 * PADDING));
    styleContainer(column);
    
    // Header
    lv_obj_t* header = lv_label_create(column);
    lv_label_set_text(header, title);
    styleLabelHeader(header);
    lv_obj_set_pos(header, 0, 0);
    
    return column;
}

// Create car section
void createCarSection(lv_obj_t* parent, const char* title, const char* car_name) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_set_pos(container, PADDING, 0);
    lv_obj_set_size(container, SCREEN_WIDTH - (2 * PADDING), LOWER_SECTION_HEIGHT - 8);
    styleContainer(container);
    
    // Title (left) and Car name (right)
    ui.car.title_label = lv_label_create(container);
    lv_label_set_text(ui.car.title_label, title);
    styleLabelPrimary(ui.car.title_label);
    lv_obj_set_pos(ui.car.title_label, 0, 0);
    
    ui.car.car_label = lv_label_create(container);
    lv_label_set_text_fmt(ui.car.car_label, "%s", car_name);
    styleLabelSecondary(ui.car.car_label);
    positionAndAlign(ui.car.car_label, SCREEN_WIDTH-(4*PADDING)-16-120, 0, 120, LV_TEXT_ALIGN_RIGHT);
    
    // Power
    ui.car.power_label = lv_label_create(container);
    lv_label_set_text(ui.car.power_label, formatPower(0).c_str());
    styleLabelSecondary(ui.car.power_label);
    lv_obj_set_pos(ui.car.power_label, 0, 25);

    // Ladedauer
    ui.car.ladedauer_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladedauer_value, "--:--");
    styleLabelSecondary(ui.car.ladedauer_value);
    positionAndAlign(ui.car.ladedauer_value, SCREEN_WIDTH-(4*PADDING)-16-120, 25, 120, LV_TEXT_ALIGN_RIGHT);

    // SoC bar
    ui.car.soc_bar = lv_bar_create(container);
    lv_obj_set_size(ui.car.soc_bar, SCREEN_WIDTH - (4 * PADDING) - 16, 20);
    lv_obj_set_pos(ui.car.soc_bar, 0, 50);
    lv_bar_set_value(ui.car.soc_bar, 35, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui.car.soc_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ui.car.soc_bar, 8, LV_PART_INDICATOR);
    
    // Initialize pulsing border style for charging animation
    lv_style_init(&pulse_style);
    lv_style_set_border_width(&pulse_style, 3);
    lv_style_set_border_color(&pulse_style, lv_color_hex(COLOR_PULSE_BORDER));
    lv_style_set_border_opa(&pulse_style, 50);
    lv_style_set_radius(&pulse_style, 8);
    lv_style_set_pad_all(&pulse_style, 3); // Add padding to prevent overlap
    
    // Plan SoC marker (smaller, darker line)
    ui.car.plan_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.plan_soc_marker, 2, 20);
    lv_obj_set_pos(ui.car.plan_soc_marker, 0, 50); // Aligned with bar
    lv_obj_set_style_bg_color(ui.car.plan_soc_marker, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(ui.car.plan_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.plan_soc_marker, 1, 0);
    lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Limit SoC marker (larger, bar-colored with rounded edges)
    ui.car.limit_soc_marker = lv_obj_create(container);
    lv_obj_set_size(ui.car.limit_soc_marker, 6, 28);
    lv_obj_set_pos(ui.car.limit_soc_marker, 0, 46); // Slightly above bar
    lv_obj_set_style_bg_color(ui.car.limit_soc_marker, lv_color_hex(0x4CAF50), 0); // Same as bar color
    lv_obj_set_style_border_width(ui.car.limit_soc_marker, 0, 0);
    lv_obj_set_style_radius(ui.car.limit_soc_marker, 3, 0); // Rounded edges
    lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN); // Initially hidden
    
    // Description labels
    ui.car.soc_desc = lv_label_create(container);
    lv_label_set_text(ui.car.soc_desc, "LADESTAND");
    styleLabelSecondary(ui.car.soc_desc);
    positionAndAlign(ui.car.soc_desc, 0, 90, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.plan_desc = lv_label_create(container);
    lv_label_set_text(ui.car.plan_desc, "PLAN");
    styleLabelSecondary(ui.car.plan_desc);
    positionAndAlign(ui.car.plan_desc, 180, 90, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.limit_desc = lv_label_create(container);
    lv_label_set_text(ui.car.limit_desc, "LADELIMIT");
    styleLabelSecondary(ui.car.limit_desc);
    positionAndAlign(ui.car.limit_desc, SCREEN_WIDTH-(4*PADDING)-16-120, 90, 120, LV_TEXT_ALIGN_RIGHT);
    
    // Status values
    ui.car.soc_value = lv_label_create(container);
    lv_label_set_text(ui.car.soc_value, "35%");
    styleLabelPrimary(ui.car.soc_value);
    positionAndAlign(ui.car.soc_value, 0, 110, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.plan_value = lv_label_create(container);
    lv_label_set_text(ui.car.plan_value, "keiner");
    styleLabelPrimary(ui.car.plan_value);
    positionAndAlign(ui.car.plan_value, 180, 110, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.plan_soc_value = lv_label_create(container);
    lv_label_set_text(ui.car.plan_soc_value, "");
    styleLabelSecondary(ui.car.plan_soc_value);
    positionAndAlign(ui.car.plan_soc_value, 180, 130, 120, LV_TEXT_ALIGN_CENTER);
    
    ui.car.range_value = lv_label_create(container);
    lv_label_set_text(ui.car.range_value, "140 km");
    styleLabelSecondary(ui.car.range_value);
    positionAndAlign(ui.car.range_value, 0, 130, 120, LV_TEXT_ALIGN_LEFT);
    
    ui.car.ladelimit_value = lv_label_create(container);
    lv_label_set_text(ui.car.ladelimit_value, "80%");
    styleLabelPrimary(ui.car.ladelimit_value);
    positionAndAlign(ui.car.ladelimit_value, SCREEN_WIDTH-(4*PADDING)-16-120, 110, 120, LV_TEXT_ALIGN_RIGHT);
}

// Create UI
void createUI() {
    // Main screen
    ui.screen = lv_obj_create(NULL);
    lv_obj_set_size(ui.screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(ui.screen, lv_color_hex(COLOR_GRID_BG), 0);
    lv_obj_set_style_pad_all(ui.screen, 0, 0);
    lv_obj_set_scrollbar_mode(ui.screen, LV_SCROLLBAR_MODE_OFF);
    
    // Upper container
    ui.upper_container = lv_obj_create(ui.screen);
    lv_obj_set_pos(ui.upper_container, 0, 0);
    lv_obj_set_size(ui.upper_container, SCREEN_WIDTH, UPPER_SECTION_HEIGHT);
    styleContainerWithBorder(ui.upper_container);
    lv_obj_set_style_pad_all(ui.upper_container, PADDING, 0);
    
    // Create columns
    lv_obj_t* in_column = createColumn(ui.upper_container, "In", PADDING);
    lv_obj_t* out_column = createColumn(ui.upper_container, "Out", COLUMN_WIDTH + (2 * PADDING));
    
    // Create energy rows (removed money values)
    createEnergyRow(in_column, "Erzeugung", "", "0W", 50, 
                    &ui.generation.desc, &ui.generation.value1, &ui.generation.value2);
    createEnergyRow(in_column, "Batterie entladen", "", "0W", 72, 
                    &ui.battery_discharge.desc, &ui.battery_discharge.value1, &ui.battery_discharge.value2);
    createEnergyRow(in_column, "Netzbezug", "", "0W", 94, 
                    &ui.grid_feed.desc, &ui.grid_feed.value1, &ui.grid_feed.value2);
    
    createEnergyRow(out_column, "Verbrauch", "", "0W", 28, 
                    &ui.consumption.desc, &ui.consumption.value1, &ui.consumption.value2);
    createEnergyRow(out_column, "Ladepunkt", "", "0W", 50, 
                    &ui.loadpoint.desc, &ui.loadpoint.value1, &ui.loadpoint.value2);
    createEnergyRow(out_column, "Batterie laden", "", "0W", 72, 
                    &ui.battery_charge.desc, &ui.battery_charge.value1, &ui.battery_charge.value2);
    createEnergyRow(out_column, "Einspeisung", "", "5W", 94, 
                    &ui.grid_feedin.desc, &ui.grid_feedin.value1, &ui.grid_feedin.value2);
    
    // Lower container
    ui.lower_container = lv_obj_create(ui.screen);
    lv_obj_set_pos(ui.lower_container, 0, UPPER_SECTION_HEIGHT);
    lv_obj_set_size(ui.lower_container, SCREEN_WIDTH, LOWER_SECTION_HEIGHT);
    styleContainerWithBorder(ui.lower_container);
    lv_obj_set_style_pad_all(ui.lower_container, PADDING, 0);
    
    // Create car section
    createCarSection(ui.lower_container, "Gartenhaus", "NotAModelY");
    
    // Load screen
    lv_scr_load(ui.screen);
    
    Serial.printf("UI created - Free heap: %d bytes\n", ESP.getFreeHeap());
}

// Update UI with current data
void updateUI() {
    // Update generation (PV)
    lv_label_set_text(ui.generation.value2, formatPower(data.pvPower).c_str());
    // Apply conditional formatting for generation row
    lv_color_t genColor = (data.pvPower == 0) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.generation.desc, genColor, 0);
    lv_obj_set_style_text_color(ui.generation.value2, genColor, 0);
    
    // Update consumption (house load)
    lv_label_set_text(ui.consumption.value2, formatPower(data.homePower).c_str());
    // Apply conditional formatting for consumption row
    lv_color_t consColor = (data.homePower == 0) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.consumption.desc, consColor, 0);
    lv_obj_set_style_text_color(ui.consumption.value2, consColor, 0);
    
    // Update battery discharge/charge based on direction
    if (data.batteryPower > 0) { // Discharging (positive values)
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(data.batteryPower).c_str());
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.battery_charge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else { // Charging (negative values)
        lv_label_set_text(ui.battery_charge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_charge.value2, formatPower(-data.batteryPower).c_str());
        lv_label_set_text(ui.battery_discharge.value1, formatPercentage(data.batterySoc).c_str());
        lv_label_set_text(ui.battery_discharge.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_color_t chargeColor = (data.batteryPower == 0) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
        lv_obj_set_style_text_color(ui.battery_charge.desc, chargeColor, 0);
        lv_obj_set_style_text_color(ui.battery_charge.value1, chargeColor, 0);
        lv_obj_set_style_text_color(ui.battery_charge.value2, chargeColor, 0);
        lv_obj_set_style_text_color(ui.battery_discharge.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value1, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.battery_discharge.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    
    // Update grid feed/feed-in based on direction
    if (data.gridPower > 0) { // Consuming from grid
        lv_label_set_text(ui.grid_feed.value2, formatPower(data.gridPower).c_str());
        lv_label_set_text(ui.grid_feedin.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_VALUE), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    } else { // Feeding into grid
        lv_label_set_text(ui.grid_feedin.value2, formatPower(-data.gridPower).c_str());
        lv_label_set_text(ui.grid_feed.value2, formatPower(0).c_str());
        // Apply conditional formatting
        lv_color_t feedinColor = (data.gridPower == 0) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
        lv_obj_set_style_text_color(ui.grid_feedin.desc, feedinColor, 0);
        lv_obj_set_style_text_color(ui.grid_feedin.value2, feedinColor, 0);
        lv_obj_set_style_text_color(ui.grid_feed.desc, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
        lv_obj_set_style_text_color(ui.grid_feed.value2, lv_color_hex(COLOR_TEXT_SECONDARY), 0);
    }
    
    // Update loadpoint
    float total_lp_power = data.lp1.chargePower + data.lp2.chargePower;
    lv_label_set_text(ui.loadpoint.value2, formatPower(total_lp_power).c_str());
    // Apply conditional formatting for loadpoint row
    lv_color_t lpColor = (total_lp_power == 0) ? lv_color_hex(COLOR_TEXT_SECONDARY) : lv_color_hex(COLOR_TEXT_VALUE);
    lv_obj_set_style_text_color(ui.loadpoint.desc, lpColor, 0);
    lv_obj_set_style_text_color(ui.loadpoint.value2, lpColor, 0);
    
    // Update car section (using active loadpoint data)
    auto* activeLP = getActiveLoadpoint();
    
    // Update power label based on charging and connection status
    if (activeLP->charging) {
        // Show power value when actively charging
        lv_label_set_text(ui.car.power_label, formatPower(activeLP->chargePower).c_str());
    } else if (activeLP->plugged) {
        // Show "Connected" when plugged but not charging
        lv_label_set_text(ui.car.power_label, "Verbunden");
    } else {
        // Show "Not connected" when not plugged in
        lv_label_set_text(ui.car.power_label, "Nicht verbunden");
    }
    
    // Update SoC and bar
    if (activeLP->soc >= 0) {
        lv_bar_set_value(ui.car.soc_bar, (int)activeLP->soc, LV_ANIM_OFF);
        lv_label_set_text(ui.car.soc_value, formatPercentage(activeLP->soc).c_str());
        
        // Start pulsing border animation when charging
        if (activeLP->charging && pulse_timer == nullptr) {
            pulse_timer = lv_timer_create(pulse_animation_cb, 100, NULL); // 100ms update for smooth animation
            lv_obj_add_style(ui.car.soc_bar, &pulse_style, LV_PART_INDICATOR);
        } else if (!activeLP->charging && pulse_timer != nullptr) {
            // Stop animation when not charging
            lv_timer_del(pulse_timer);
            pulse_timer = nullptr;
            lv_obj_remove_style(ui.car.soc_bar, &pulse_style, LV_PART_INDICATOR);
        }
    } else {
        lv_label_set_text(ui.car.soc_value, "---");
        // Stop animation if no SoC data
        if (pulse_timer != nullptr) {
            lv_timer_del(pulse_timer);
            pulse_timer = nullptr;
            lv_obj_remove_style(ui.car.soc_bar, &pulse_style, LV_PART_INDICATOR);
        }
    }
    
    // Update plan SoC marker
    if (activeLP->effectivePlanSoc > 0) { // Only show if > 0, not just >= 0
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectivePlanSoc / 100.0) * barWidth - 1; // Center the 2px marker
        lv_obj_set_pos(ui.car.plan_soc_marker, markerX, 50);
        lv_obj_clear_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.car.plan_soc_marker, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update limit SoC marker
    if (activeLP->effectiveLimitSoc >= 0) {
        int barWidth = SCREEN_WIDTH - (4 * PADDING) - 16;
        int markerX = (activeLP->effectiveLimitSoc / 100.0) * barWidth - 3; // Center the 6px marker (offset by half width)
        
        // Ensure marker doesn't go outside bar bounds
        if (markerX < 0) markerX = 0;
        if (markerX > barWidth - 6) markerX = barWidth - 6;
        
        lv_obj_set_pos(ui.car.limit_soc_marker, markerX, 46);
        lv_obj_clear_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
        
        Serial.printf("Limit SoC marker: %d%% at X=%d (barWidth=%d)\n", (int)activeLP->effectiveLimitSoc, markerX, barWidth);
    } else {
        lv_obj_add_flag(ui.car.limit_soc_marker, LV_OBJ_FLAG_HIDDEN);
        Serial.println("Limit SoC marker: hidden (no valid limit)");
    }
    
    // Update range - only show if vehicleRange is available from EVCC
    if (activeLP->vehicleRange >= 0) {
        lv_label_set_text(ui.car.range_value, formatDistance(activeLP->vehicleRange).c_str());
    } else {
        lv_label_set_text(ui.car.range_value, formatDistance(-1).c_str());
    }
    
    // Update titles
    if (!activeLP->vehicleTitle.isEmpty()) {
        lv_label_set_text(ui.car.car_label, activeLP->vehicleTitle.c_str());
    }
    if (!activeLP->title.isEmpty()) {
        lv_label_set_text(ui.car.title_label, activeLP->title.c_str());
    }
    
    // Update plan time and plan SoC
    if (!activeLP->effectivePlanTime.isEmpty()) {
        lv_label_set_text(ui.car.plan_value, formatPlanTime(activeLP->effectivePlanTime).c_str());
        
        // Show plan SoC if plan time exists
        if (activeLP->effectivePlanSoc >= 0) {
            lv_label_set_text(ui.car.plan_soc_value, formatPercentage(activeLP->effectivePlanSoc).c_str());
        } else {
            lv_label_set_text(ui.car.plan_soc_value, "");
        }
    } else {
        lv_label_set_text(ui.car.plan_value, "keiner");
        lv_label_set_text(ui.car.plan_soc_value, "");
    }
    
    // Update charge limit
    if (activeLP->effectiveLimitSoc >= 0) {
        lv_label_set_text(ui.car.ladelimit_value, formatPercentage(activeLP->effectiveLimitSoc).c_str());
    } else {
        lv_label_set_text(ui.car.ladelimit_value, "---");
    }
    
    // Update Ladedauer with planProjectedStart if available
    if (!activeLP->planProjectedStart.isEmpty()) {
        String formattedTime = formatPlanTime(activeLP->planProjectedStart);
        char projectedDisplay[64];
        snprintf(projectedDisplay, sizeof(projectedDisplay), "|--> %s", formattedTime.c_str());
        lv_label_set_text(ui.car.ladedauer_value, projectedDisplay);
    } else {
        // Clear the planned start display when no plan exists
        lv_label_set_text(ui.car.ladedauer_value, "--:--");
    }
}

// Poll EVCC data
bool pollEVCCData() {
    Serial.printf("Starting poll - Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Check memory before HTTP request
    if (ESP.getFreeHeap() < 16000) {
        Serial.println("Insufficient memory for HTTP request");
        return false;
    }
    
    String response;
    response.reserve(2048); // Pre-allocate to avoid fragmentation
    
    // Get combined data in single request
    if (httpGet(combined_path, response)) {
        if (parseCombinedData(response)) {
            data.lastUpdate = millis();
            data.consecutiveFailures = 0;
            updateUI();
            
            // Force string cleanup
            response = String();
            
            // Memory health check
            if (ESP.getFreeHeap() < 12000) {
                Serial.printf("WARNING: Low memory after poll: %d bytes\n", ESP.getFreeHeap());
            }
            
            return true;
        }
    } else {
        Serial.println("HTTP request failed");
    }
    
    return false;
}

// WiFi status display variables
lv_obj_t* wifi_screen = nullptr;
lv_obj_t* wifi_status_label = nullptr;
lv_obj_t* wifi_ssid_label = nullptr;
lv_obj_t* wifi_ip_label = nullptr;

// Show initial WiFi connecting status screen
void showWiFiConnectingStatus() {
    // Create simple status screen
    wifi_screen = lv_obj_create(NULL);
    lv_obj_set_size(wifi_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(wifi_screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_pad_all(wifi_screen, 20, 0);
    lv_obj_set_scrollbar_mode(wifi_screen, LV_SCROLLBAR_MODE_OFF);
    
    // Main status label
    wifi_status_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_status_label, "Initializing...");
    lv_obj_set_style_text_font(wifi_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(wifi_status_label, 0, 100);
    lv_obj_set_width(wifi_status_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_status_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // SSID label
    wifi_ssid_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_ssid_label, "");
    lv_obj_set_style_text_font(wifi_ssid_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wifi_ssid_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_pos(wifi_ssid_label, 0, 140);
    lv_obj_set_width(wifi_ssid_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_ssid_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // IP/Status label
    wifi_ip_label = lv_label_create(wifi_screen);
    lv_label_set_text(wifi_ip_label, "");
    lv_obj_set_style_text_font(wifi_ip_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(wifi_ip_label, lv_color_hex(0x999999), 0);
    lv_obj_set_pos(wifi_ip_label, 0, 180);
    lv_obj_set_width(wifi_ip_label, SCREEN_WIDTH - 40);
    lv_obj_set_style_text_align(wifi_ip_label, LV_TEXT_ALIGN_CENTER, 0);
    
    // Load the screen
    lv_scr_load(wifi_screen);
    lv_task_handler(); // Process immediately
}

// Update WiFi status on screen
void updateWiFiStatus(const String& status, const String& ssid_name, const String& ip_info) {
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, status.c_str());
    }
    if (wifi_ssid_label && !ssid_name.isEmpty()) {
        String ssid_text = "Network: " + ssid_name;
        lv_label_set_text(wifi_ssid_label, ssid_text.c_str());
    }
    if (wifi_ip_label && !ip_info.isEmpty()) {
        lv_label_set_text(wifi_ip_label, ip_info.c_str());
    }
    lv_task_handler(); // Process immediately
}

void setup() {
    Serial.begin(115200);
    Serial.println("EVCC Display ESP32 - Starting...");
    
    // Initialize watchdog timer (8 seconds)
    esp_task_wdt_deinit(); // Clear any existing watchdog
    esp_task_wdt_init(8, true);
    esp_task_wdt_add(NULL);
    
    // Initialize display FIRST for immediate feedback
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(TFT_BLACK);
    
    // Configure backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // Initialize LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_WIDTH * 10);
    
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_WIDTH;
    disp_drv.ver_res = SCREEN_HEIGHT;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    Serial.printf("Display initialized - Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Show WiFi connecting status immediately
    showWiFiConnectingStatus();
    
    // Connect to WiFi
    if (connectWiFi()) {
        Serial.printf("After WiFi - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update status for time sync
        updateWiFiStatus("Synchronizing time...", ssid, WiFi.localIP().toString());
        
        // Initialize time with proper timezone (CET/CEST for Germany)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Europe/Berlin timezone
        tzset();
        Serial.println("Waiting for time synchronization...");
        time_t now = time(nullptr);
        int attempts = 0;
        while (now < 8 * 3600 * 2 && attempts < 20) {
            delay(500);
            now = time(nullptr);
            attempts++;
        }
        Serial.printf("Time synchronized: %s", ctime(&now));
        
        // Update status for HTTP test
        updateWiFiStatus("Testing EVCC connection...", ssid, WiFi.localIP().toString());
        
        // Test HTTP before UI creation
        Serial.println("Testing HTTP before UI creation...");
        String response;
        if (httpGet(combined_path, response)) {
            Serial.println("✅ HTTP test successful before UI!");
            parseCombinedData(response);
        }
        
        Serial.printf("After HTTP test - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update status for UI creation
        updateWiFiStatus("Creating interface...", ssid, WiFi.localIP().toString());
        
        // Create UI
        createUI();
        
        // Clean up WiFi status screen
        if (wifi_screen) {
            lv_obj_del(wifi_screen);
            wifi_screen = nullptr;
            wifi_status_label = nullptr;
            wifi_ssid_label = nullptr;
            wifi_ip_label = nullptr;
        }
        
        Serial.printf("After UI creation - Free heap: %d bytes\n", ESP.getFreeHeap());
        
        // Update UI with initial data
        updateUI();
        Serial.println("📊 UI updated with initial data");
        
        Serial.printf("Setup complete - Free heap: %d bytes\n", ESP.getFreeHeap());
    } else {
        Serial.println("WiFi failed - running in demo mode");
        
        // Keep error message visible for a moment, then switch to demo UI
        delay(3000);
        
        // Create demo UI
        createUI();
        
        // Clean up WiFi status screen
        if (wifi_screen) {
            lv_obj_del(wifi_screen);
            wifi_screen = nullptr;
            wifi_status_label = nullptr;
            wifi_ssid_label = nullptr;
            wifi_ip_label = nullptr;
        }
    }
}

void loop() {
    static unsigned long lastPoll = 0;
    static unsigned long lastLVGL = 0;
    
    unsigned long now = millis();
    
    // Handle millis() overflow (every ~49 days)
    if (now < lastPoll) {
        lastPoll = 0;
    }
    if (now < lastLVGL) {
        lastLVGL = 0;
    }
    
    // Handle LVGL tasks (every 5ms)
    if (now - lastLVGL >= 5) {
        lv_task_handler();
        lastLVGL = now;
        esp_task_wdt_reset(); // Feed watchdog
    }
    
    // Poll EVCC data
    if (WiFi.status() == WL_CONNECTED && now - lastPoll >= POLL_INTERVAL) {
        lastPoll = now;
        if (!pollEVCCData()) {
            data.consecutiveFailures++;
            Serial.printf("Poll failed (#%d)\n", data.consecutiveFailures);
            
            // Emergency restart if too many failures
            if (data.consecutiveFailures > 50) {
                Serial.println("Too many failures, restarting...");
                delay(1000);
                ESP.restart();
            }
        }
    }
    
    // WiFi reconnection logic
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (now - lastReconnectAttempt > 30000) { // Try every 30 seconds
            Serial.println("WiFi disconnected, attempting reconnect...");
            WiFi.begin(ssid, password);
            lastReconnectAttempt = now;
        }
    }
    
    delay(1);
}