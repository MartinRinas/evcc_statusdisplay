# EVCC Display for ESP32

A smart energy display for EVCC (Electric Vehicle Charge Controller) built on ESP32 with ST7796 TFT display and LVGL graphics library. This was initially written in MicroPython, however C++ implementation replaces the MicroPython version for better memory management and reliability. MicroPython was too memory constrained and could either draw the UI or poll HTTP data, but not both.

## Features

- **Real-time Energy Monitoring**: PV generation, battery status, grid power, home consumption
- **Dual Loadpoint Support**: Automatic rotation between charging points based on activity  
- **Display Logic**: Shows active charging sessions, rotates when idle or both active
- **German Localization**: Timezone-aware timestamps and German text
- **Visual Indicators**: SoC bar with plan/limit markers, stripe pattern when charging, conditional color coding
- **WiFi Connectivity**: Automatic reconnection and status monitoring
- **Memory Optimized**: Designed for long-term reliability with watchdog protection and null safety
- **Mimics EVCC main UI**: Design mimics the main EVCC page design within the constraints of a 480x320px display
- **Centralized Configuration**: Separated settings in `config.h` and `wifi_config.h` for easy customization

## Hardware Requirements

- **ESP32 microcontroller** (developed using Freenove ESP32 Display FNK0103S)
- **ST7796 TFT display** (480x320 resolution)
- **Compatible pin configuration**

### Pin Configuration
```cpp
TFT_MOSI: 13    TFT_DC:   2
TFT_SCLK: 14    TFT_RST:  4  
TFT_CS:   15    TFT_BL:   27
```

## Software Dependencies

### PlatformIO (Recommended)
```ini
lib_deps = 
    lvgl/lvgl@^8.3.0
    bodmer/TFT_eSPI@^2.5.0
    bblanchon/ArduinoJson@^6.21.0
```

### Arduino IDE Libraries
- **LVGL** (v8.x) - Graphics library
- **TFT_eSPI** - Display driver  
- **ArduinoJson** (v6.x) - API parsing
- **HTTPClient** (ESP32 core) - HTTP requests
- **WiFi** (ESP32 core) - Network connectivity

## Setup Instructions

### 1. WiFi Configuration
```bash
# Copy the template and configure your credentials
cp src/wifi_config.h.template src/wifi_config.h
# Edit wifi_config.h with your network details
```

Example `wifi_config.h`:
```cpp
const char* WIFI_SSID = "your_network";
const char* WIFI_PASSWORD = "your_password";  
const char* EVCC_HOST = "192.168.1.100";
const int EVCC_PORT = 7070;
const char* EVCC_API_PATH = "/api/state?jq=...";
```

**Note**: All other configuration settings (display dimensions, timing, colors, pins) are in `src/config.h` and typically don't need modification unless customizing hardware or appearance.

### 2. TFT_eSPI Configuration

**Option A: PlatformIO (Automatic)**
Configuration handled via `platformio.ini` build flags.

**Option B: Arduino IDE (Manual)**
Create `User_Setup.h` in TFT_eSPI library folder:
```cpp
#define ST7796_DRIVER
#define TFT_WIDTH  320
#define TFT_HEIGHT 480
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST   4
#define TFT_BL   27
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define SPI_FREQUENCY  20000000
```

### 3. EVCC Server Setup
Ensure your EVCC server is running and accessible:
- **API Endpoint**: `http://YOUR_IP:7070/api/state`
- **Update Interval**: 10 seconds
- **Network Access**: ESP32 must reach EVCC server

### 4. Build & Upload

**PlatformIO:**
```bash
pio run --target upload --target monitor
```

**Arduino IDE:**
- Board: "ESP32 Dev Module"
- Flash Size: "4MB" 
- Upload Speed: "921600"
- Tools → Upload

## Display Layout
Using demo data from demo.evcc.io:
![PXL_20251017_085915184](https://github.com/user-attachments/assets/bf5b8e9a-a1f4-479b-8108-dc47448814f0)

live data:
![PXL_20251017_085728563](https://github.com/user-attachments/assets/4781f419-f48c-4b7c-b7f9-62f7107f1f93)


- **Conditional Colors**: Inactive flows shown in secondary color
- **Real-time Values**: Power in W/kW, percentages for battery

**Auto-Rotation Logic**:
- **Only one charging** → Show active loadpoint
- **Both/neither charging** → Rotate every 10 seconds
- **SoC Bar**: Visual charge level with plan/limit markers
- **Charging Indicator**: Animated stripe pattern overlay when actively charging
- **Status Display**: Connection status, charging power, plan times


## Long-term Reliability Features

- **Memory Management**: String pre-allocation and cleanup
- **Watchdog Timer**: Automatic recovery from hangs  
- **Error Handling**: Auto-restart after consecutive failures
- **WiFi Recovery**: Automatic reconnection on network drops
- **Overflow Protection**: Handles millis() rollover (49+ day runtime)
- **Failure Tracking**: Monitors and responds to repeated HTTP failures

## Development & Debugging
- **Demo data**: Supports switching to data from demo.evcc.io for validation and showcase
- **Webserver**: Status webserver with access to logs, current JSON retrieved from the API and option to turn on serial logging (debug)
<img width="1538" height="1384" alt="Screenshot 2025-10-17 105624" src="https://github.com/user-attachments/assets/194e5402-86c7-4c76-bca8-d890826543bd" />
<img width="1538" height="1384" alt="Screenshot 2025-10-17 105632" src="https://github.com/user-attachments/assets/92009497-1056-4bfa-8153-9292b9e6b40c" />
<img width="1538" height="1384" alt="Screenshot 2025-10-17 105640" src="https://github.com/user-attachments/assets/634c26e9-7af5-429b-9e69-1f02677a0f94" />



### To Dos
- support for german special characters (Umlaute), currently not included in fonts
- evaluate if icons can be used to replace text labels, icon dimensions are a challenging - unsure if 12x12/14x14px allow to identify sun, battery, home, grid clearly.

### Common Issues & Solutions

| Problem | Cause | Solution |
|---------|-------|----------|
| **Compilation Error** | TFT_eSPI config | Check User_Setup.h or build flags |
| **Black Screen** | Pin wiring | Verify pin connections |
| **WiFi Fails** | Credentials | Check wifi_config.h |
| **No Data** | EVCC unreachable | Verify server IP/port |

## Advanced Configuration

All configuration settings are centralized in `src/config.h`. Edit this file to customize:

### Display Timing (in config.h)
```cpp
#define POLL_INTERVAL 10000      // EVCC API polling interval (ms)
#define HTTP_TIMEOUT 8000        // HTTP request timeout (ms)
#define ROTATION_INTERVAL 10000  // Loadpoint rotation interval (ms)
```

### Color Customization (in config.h)
```cpp
#define COLOR_GRID_BG     0xf3f3f7   // Background color
#define COLOR_PANEL_BG    0xFFFFFF   // Panel background
#define COLOR_TEXT_PRIMARY 0x000000  // Primary text
#define COLOR_TEXT_SECONDARY 0x93949e // Secondary text

// Bar colors for energy flow visualization
#define COLOR_BAR_GENERATION  0x4CAF50  // Green - PV
#define COLOR_BAR_LOADPOINT   0x9C27B0  // Purple - Charging
// ... see config.h for all color definitions
```

### Hardware Pin Configuration (in config.h)
```cpp
#define TFT_MOSI 13    // Modify if using different pins
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   27
```

## Troubleshooting

### Build Issues
1. **Library conflicts**: Clean build directory
2. **Pin definitions**: Check platformio.ini build flags
3. **Memory errors**: Increase available heap

### Runtime Issues  
1. **Display corruption**: Check SPI frequency (lower if needed)
2. **WiFi drops**: Verify signal strength and credentials
3. **HTTP timeouts**: Check EVCC server accessibility
4. **Memory leaks**: Monitor serial output for heap warnings
