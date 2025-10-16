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
#include <ESPAsyncWebServer.h>
#include "wifi_config.h"
#include "config.h"
#include "logging.h"
#include "webserver.h"
#include "ui_helpers.h"
#include "display_updates.h"

// Forward declarations for functions defined later (ordering disruption after refactor)
bool httpGet(const char* path, String& response);
bool connectWiFi();
void startWebServer();

// Threshold (in Watts) below which values are considered inactive for dimming
#ifndef POWER_ACTIVE_THRESHOLD
#define POWER_ACTIVE_THRESHOLD 10.0f
#endif

// Declare the stripe pattern image
LV_IMG_DECLARE(img_skew_strip);



// WiFi credentials - see wifi_config.h
// Note: Copy wifi_config.h.template to wifi_config.h and configure
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// EVCC server configuration - see wifi_config.h
const char* evcc_host = EVCC_HOST;
const int evcc_port = EVCC_PORT;
const char* combined_path = EVCC_API_PATH;

// Display and LVGL setup
TFT_eSPI tft = TFT_eSPI();
static lv_color_t buf[SCREEN_WIDTH * 10];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

EVCCData data;

// Stripe pattern style
lv_style_t stripe_style;
lv_timer_t* stripe_timer = nullptr;
int stripe_offset = 0;

// Loadpoint rotation state
RotationState rotationState;

// Web server for status/logs
AsyncWebServer server(WEB_SERVER_PORT);

// Demo mode flag: when true, API base switches to https://demo.evcc.io
bool demoMode = false;

// Log buffer globals (definitions - declarations in logging.h)
LogEntry logBuffer[LOG_BUFFER_SIZE];
int logHead = 0;
int logCount = 0;
bool debugEnabled = DEBUG_MODE;
uint32_t logTotal = 0;
uint32_t logOverwrites = 0;
uint32_t logDropped = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

// UI element references (moved struct to ui_helpers.h)
UIElements ui;

// (Styling and UI creation helper implementations moved to ui_helpers.*)

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

// (UI update, formatting, rotation, stripe pattern logic moved to display_updates.*)

// Network & web server functions (restored after refactor extraction)
void startWebServer() {
    setupWebServer(server);
    server.begin();
    logMessage("Web server started on port " + String(WEB_SERVER_PORT));
}

bool connectWiFi() {
    logMessage("Connecting to WiFi...");
    updateWiFiStatus("Connecting to WiFi...", ssid, "");
    WiFi.begin(ssid, password);
    int attempts = 0;
    const int maxAttempts = 40; // ~20s (500ms steps)
    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        delay(500);
        esp_task_wdt_reset();
        lv_task_handler();
        yield();
        char progressText[56];
        snprintf(progressText, sizeof(progressText), "Connecting to WiFi, attempt %d/%d", attempts + 1, maxAttempts);
        updateWiFiStatus(progressText, ssid, "");
        attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        logMessage("Connected! IP: " + WiFi.localIP().toString());
        updateWiFiStatus("WiFi Connected!", ssid, WiFi.localIP().toString());
        delay(1000);
        return true;
    }
    logMessage("Failed to connect to WiFi");
    updateWiFiStatus("WiFi Connection Failed", ssid, "Check credentials");
    delay(2000);
    return false;
}

bool httpGet(const char* path, String& response) {
    HTTPClient http;
    String url;
    if (demoMode) {
        url = String("https://demo.evcc.io") + String(path);
    } else {
        url = String("http://") + evcc_host + ":" + String(evcc_port) + String(path);
    }
    logMessage(String("Requesting [") + (demoMode?"DEMO":"LIVE") + "]: " + url);
    http.begin(url);
    http.setTimeout(HTTP_TIMEOUT);
    int httpCode = http.GET();
    bool success = false;
    if (httpCode == HTTP_CODE_OK) {
        response = http.getString();
        logMessage("HTTP success: " + String(response.length()) + " chars");
        success = true;
    } else {
        logMessage((uint8_t)LOG_LEVEL_ERROR, "HTTP error: " + String(httpCode));
    }
    http.end();
    return success;
}

// Initialize stripe pattern style  
void initStripeStyle() {
    lv_style_init(&stripe_style);
    lv_style_set_bg_img_src(&stripe_style, &img_skew_strip);
    lv_style_set_bg_img_tiled(&stripe_style, true);
    lv_style_set_bg_img_opa(&stripe_style, LV_OPA_30);
    lv_style_set_bg_img_recolor_opa(&stripe_style, 0);
}

// Stripe pattern state tracking
bool stripe_applied = false;

// (Stripe pattern logic moved)



// (Composite bar / energy row / column / car section helpers now implemented in ui_helpers.cpp)
// Parse combined data
bool parseCombinedData(const String& json) {
    DynamicJsonDocument doc(1536);
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
    logMessage((uint8_t)LOG_LEVEL_ERROR, "Combined parse error: " + String(error.c_str()));
        return false;
    }
    
    // Parse energy data
    data.gridPower = doc["gridPower"] | 0.0;
    data.pvPower = doc["pvPower"] | 0.0;
    data.batterySoc = doc["batterySoc"] | -1.0;
    data.homePower = doc["homePower"] | 0.0;
    data.batteryPower = doc["batteryPower"] | 0.0;
    
    // Parse solar forecast data
    JsonObject solar = doc["solar"];
    if (!solar.isNull()) {
        data.solarForecastScale = solar["scale"] | 1.0;
        data.solarForecastTodayEnergy = solar["todayEnergy"] | 0.0;
    } else {
        data.solarForecastScale = 1.0;
        data.solarForecastTodayEnergy = 0.0;
    }
    
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
        data.lp1.maxCurrent = lp1["maxCurrent"] | 0.0;
        data.lp1.offeredCurrent = lp1["offeredCurrent"] | 0.0;
        data.lp1.phasesActive = lp1["phasesActive"] | 0;
        JsonArray currents = lp1["chargeCurrents"];
        for (int i = 0; i < 3 && i < currents.size(); i++) {
            data.lp1.chargeCurrents[i] = currents[i] | 0.0;
        }
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
        data.lp2.maxCurrent = lp2["maxCurrent"] | 0.0;
        data.lp2.offeredCurrent = lp2["offeredCurrent"] | 0.0;
        data.lp2.phasesActive = lp2["phasesActive"] | 0;
        JsonArray currents = lp2["chargeCurrents"];
        for (int i = 0; i < 3 && i < currents.size(); i++) {
            data.lp2.chargeCurrents[i] = currents[i] | 0.0;
        }
    }
    
    // Calculate derived values
    // float total_charge_power = data.lp1.chargePower + data.lp2.chargePower;
    
    
    return true;
}


// (Car section creation now implemented in ui_helpers.cpp)

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
    
    // Create columns (IN with header, OUT without - we'll add OUT label separately)
    lv_obj_t* in_column = createColumn(ui.upper_container, "In", PADDING, true);
    lv_obj_t* out_column = createColumn(ui.upper_container, "Out", COLUMN_WIDTH + (2 * PADDING), false);
    
    // Calculate bar dimensions - center bars horizontally
    // Total usable width minus padding on both sides
    int totalUsableWidth = SCREEN_WIDTH - (4 * PADDING);
    int barWidth = 360; // Fixed width for centered bars
    int barStartX = (SCREEN_WIDTH - barWidth) / 2; // Center horizontally
    
    // OUT label position (right side, aligned with value columns)
    int outLabelX = SCREEN_WIDTH - (3 * PADDING) - 60; // Right-aligned
    
    // Create IN bar centered at top
    ui.in_bar.container = createCompositeBar(ui.upper_container, barStartX, 2, barWidth, 20);
    ui.in_bar.generation_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_GENERATION), &ui.in_bar.generation_label);
    ui.in_bar.battery_out_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_BATTERY_OUT), &ui.in_bar.battery_out_label);
    ui.in_bar.grid_in_segment = createBarSegment(ui.in_bar.container, lv_color_hex(COLOR_BAR_GRID_IN), &ui.in_bar.grid_in_label);
    // Remove background for IN bar container
    lv_obj_set_style_bg_opa(ui.in_bar.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(ui.in_bar.container, lv_color_hex(0x000000), 0); // color irrelevant when transparent
    
    // Create OUT bar below IN bar (offset by bar height + gap)
    int outBarY = 2 + 20 + 4; // IN bar Y + height + gap
    ui.out_bar.container = createCompositeBar(ui.upper_container, barStartX, outBarY, barWidth, 20);
    ui.out_bar.consumption_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_CONSUMPTION), &ui.out_bar.consumption_label);
    ui.out_bar.loadpoint_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_LOADPOINT), &ui.out_bar.loadpoint_label);
    ui.out_bar.battery_in_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_BATTERY_IN), &ui.out_bar.battery_in_label);
    ui.out_bar.grid_out_segment = createBarSegment(ui.out_bar.container, lv_color_hex(COLOR_BAR_GRID_OUT), &ui.out_bar.grid_out_label);
    // Remove background for OUT bar container
    lv_obj_set_style_bg_opa(ui.out_bar.container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(ui.out_bar.container, lv_color_hex(0x000000), 0);
    
    // Create OUT label right-aligned, vertically aligned with OUT bar
    lv_obj_t* out_header = lv_label_create(ui.upper_container);
    lv_label_set_text(out_header, "Out");
    styleLabelHeader(out_header);
    positionAndAlign(out_header, outLabelX, outBarY, 60, LV_TEXT_ALIGN_RIGHT);

    // Restyle IN and OUT segments to transparent frames with border
    auto styleFrameSegment = [](lv_obj_t* seg, lv_obj_t* label){
        if(!seg) return;
        lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(seg, 1, 0);
        lv_obj_set_style_border_color(seg, lv_color_hex(BS_GRAY_MEDIUM), 0);
        lv_obj_set_style_pad_all(seg, 0, 0);
        lv_obj_set_style_radius(seg, 8, 0); // rounded edges
        if(label){
            // Override any previous contrast color to static dark text
            lv_obj_set_style_text_color(label, lv_color_hex(BS_GRAY_DARK), 0);
        }
    };
    styleFrameSegment(ui.in_bar.generation_segment, ui.in_bar.generation_label);
    styleFrameSegment(ui.in_bar.battery_out_segment, ui.in_bar.battery_out_label);
    styleFrameSegment(ui.in_bar.grid_in_segment, ui.in_bar.grid_in_label);
    styleFrameSegment(ui.out_bar.consumption_segment, ui.out_bar.consumption_label);
    styleFrameSegment(ui.out_bar.loadpoint_segment, ui.out_bar.loadpoint_label);
    styleFrameSegment(ui.out_bar.battery_in_segment, ui.out_bar.battery_in_label);
    styleFrameSegment(ui.out_bar.grid_out_segment, ui.out_bar.grid_out_label);

    // ---------------------------------------------------------------------
    // Overlay bar (aggregated self consumption / import / export flows)
    // Positioned vertically centered across the space spanned by IN and OUT bars
    // IN bar: y=2 height=20, gap=4, OUT bar: y=26 height=20
    // Total span = 44px from y=2 to y=46. Choose overlay height 16px.
    // Center Y = 2 + 44/2 = 24 -> top = 24 - 8 = 16
    const int overlayHeight = 16;
    int overlayY = 16; // computed as above
    ui.overlay_bar.container = createCompositeBar(ui.upper_container, barStartX, overlayY, barWidth, overlayHeight);
    if (ui.overlay_bar.container) {
        // Bring to foreground so it visually overlaps both bars
        lv_obj_move_foreground(ui.overlay_bar.container);
        // Slight transparency so underlying bars can still be perceived
        lv_obj_set_style_bg_opa(ui.overlay_bar.container, LV_OPA_TRANSP, 0);
        // Create four segments using existing semantic colors
        ui.overlay_bar.selfpv_segment       = createBarSegment(ui.overlay_bar.container, lv_color_hex(COLOR_BAR_GENERATION), &ui.overlay_bar.selfpv_label);
        ui.overlay_bar.selfbattery_segment  = createBarSegment(ui.overlay_bar.container, lv_color_hex(COLOR_BAR_BATTERY_OUT), &ui.overlay_bar.selfbattery_label);
        ui.overlay_bar.grid_import_segment  = createBarSegment(ui.overlay_bar.container, lv_color_hex(COLOR_BAR_GRID_IN), &ui.overlay_bar.grid_import_label);
        ui.overlay_bar.pv_export_segment    = createBarSegment(ui.overlay_bar.container, lv_color_hex(COLOR_BAR_GRID_OUT), &ui.overlay_bar.pv_export_label);
    }
    
    // Create energy rows (moved down by 30px: from 30/52/74/96 to 60/82/104/126)
    createEnergyRow(in_column, "Erzeugung", "", "0W", 60, 
                    &ui.generation.desc, &ui.generation.value1, &ui.generation.value2);
    createEnergyRow(in_column, "Batterie entladen", "", "0W", 82, 
                    &ui.battery_discharge.desc, &ui.battery_discharge.value1, &ui.battery_discharge.value2);
    createEnergyRow(in_column, "Netzbezug", "", "0W", 104, 
                    &ui.grid_feed.desc, &ui.grid_feed.value1, &ui.grid_feed.value2);

    createEnergyRow(out_column, "Verbrauch", "", "0W", 60, 
                    &ui.consumption.desc, &ui.consumption.value1, &ui.consumption.value2);
    createEnergyRow(out_column, "Ladepunkt", "", "0W", 82, 
                    &ui.loadpoint.desc, &ui.loadpoint.value1, &ui.loadpoint.value2);
    createEnergyRow(out_column, "Batterie laden", "", "0W", 104, 
                    &ui.battery_charge.desc, &ui.battery_charge.value1, &ui.battery_charge.value2);
    createEnergyRow(out_column, "Einspeisung", "", "5W", 126, 
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
    
    logMessage("UI created - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
}

// (updateUI moved)

// Poll EVCC data
bool pollEVCCData() {
    logMessage("Starting poll - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    // Check memory before HTTP request
    if (ESP.getFreeHeap() < 16000) {
    logMessage((uint8_t)LOG_LEVEL_WARN, "Insufficient memory for HTTP request");
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
                logMessage((uint8_t)LOG_LEVEL_WARN, "Low memory after poll: " + String(ESP.getFreeHeap()) + " bytes");
            }
            
            return true;
        }
    } else {
    logMessage((uint8_t)LOG_LEVEL_ERROR, "HTTP request failed");
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

// Clean up WiFi status screen and reset pointers
void cleanupWiFiStatusScreen() {
    if (wifi_screen) {
        lv_obj_del(wifi_screen);
        wifi_screen = nullptr;
        wifi_status_label = nullptr;
        wifi_ssid_label = nullptr;
        wifi_ip_label = nullptr;
    }
}

void setup() {
    Serial.begin(115200);
    logMessage("EVCC Display ESP32 - Starting...", true);
    
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
    
    // Initialize stripe pattern style
    initStripeStyle();
    
    logMessage("Display initialized - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    // Show WiFi connecting status immediately
    showWiFiConnectingStatus();
    
    // Connect to WiFi
    if (connectWiFi()) {
        logMessage("After WiFi - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        
        // Start web server for status/logs
        startWebServer();
        
        // Update status for time sync
        updateWiFiStatus("Synchronizing time...", ssid, WiFi.localIP().toString());
        
        // Initialize time with proper timezone (CET/CEST for Germany)
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); // Europe/Berlin timezone
        tzset();
        logMessage("Waiting for time synchronization...");
        time_t now = time(nullptr);
        int attempts = 0;
        while (now < 8 * 3600 * 2 && attempts < 20) {
            delay(500);
            now = time(nullptr);
            attempts++;
            // Feed watchdog and keep UI alive while waiting for time
            esp_task_wdt_reset();
            lv_task_handler();
            yield();
            // Provide a progress update every second attempt (~1s)
            if (attempts % 2 == 0) {
                char syncMsg[48];
                snprintf(syncMsg, sizeof(syncMsg), "Synchronizing time (%d/20)", attempts);
                updateWiFiStatus(syncMsg, ssid, WiFi.localIP().toString());
            }
        }
    logMessage("Time synchronized: " + String(ctime(&now)));
                
        // Update status for HTTP test
        updateWiFiStatus("Testing EVCC connection...", ssid, WiFi.localIP().toString());
        
        // Test HTTP before UI creation
        logMessage("Testing HTTP before UI creation...");
        String response;
        if (httpGet(combined_path, response)) {
            logMessage("✅ HTTP test successful before UI!");
            parseCombinedData(response);
        }
        
        logMessage("After HTTP test - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        
        // Update status for UI creation
        updateWiFiStatus("Creating interface...", ssid, WiFi.localIP().toString());
        
        // Create UI
        createUI();
        
        // Clean up WiFi status screen
        cleanupWiFiStatusScreen();
        
        logMessage("After UI creation - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        
        // Update UI with initial data
        updateUI();
        logMessage("📊 UI updated with initial data");
        
        logMessage("Setup complete - Free heap: " + String(ESP.getFreeHeap()) + " bytes");
    } else {
        logMessage("WiFi failed - running in demo mode");
        
        // Keep error message visible for a moment, then switch to demo UI
        delay(3000);
        
        // Create demo UI
        createUI();
        
        // Clean up WiFi status screen
        cleanupWiFiStatusScreen();
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
            logMessage((uint8_t)LOG_LEVEL_WARN, "Poll failed (#" + String(data.consecutiveFailures) + ")");
            
            // Emergency restart if too many failures
            if (data.consecutiveFailures > 50) {
                logMessage((uint8_t)LOG_LEVEL_ERROR, "Too many failures, restarting...", true);
                delay(1000);
                ESP.restart();
            }
        }
    }
    
    // WiFi reconnection logic
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastReconnectAttempt = 0;
        if (now - lastReconnectAttempt > 30000) { // Try every 30 seconds
            logMessage((uint8_t)LOG_LEVEL_WARN, "WiFi disconnected, attempting reconnect...");
            WiFi.begin(ssid, password);
            lastReconnectAttempt = now;
        }
    }
    
    delay(1);
}