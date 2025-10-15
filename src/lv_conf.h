/**
 * LVGL Configuration for EVCC Display
 * Optimized for ESP32 with ST7796 TFT
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
#define LV_COLOR_DEPTH 16

/* Swap the 2 bytes of RGB565 color. Useful if the display has an 8-bit interface (e.g. SPI) */
#define LV_COLOR_16_SWAP 0

/* Enable only the Montserrat font sizes actually used (reduce flash) */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
/* Explicitly disable all other Montserrat sizes to avoid accidental enable via upstream defaults */
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* Disable unused fallback / CJK / symbol fonts */
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8 0
#define LV_FONT_UNSCII_16 0

/* Default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_12

/* Memory settings */
#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM == 1
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/* HAL settings */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM == 1
    #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

/* Display refresh settings */
#define LV_DISP_REFR_PERIOD 30

/* Input device settings */
#define LV_INDEV_DEF_READ_PERIOD 30

/* Animation */
#define LV_USE_ANIMATION 1

/* Widgets: keep only those actually used (obj, label, bar, img for stripe, maybe line). */
#define LV_USE_ARC        0
#define LV_USE_BAR        1
#define LV_USE_BTN        0
#define LV_USE_BTNMATRIX  0
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       0
#define LV_USE_LIST       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     0
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     0
#define LV_USE_SWITCH     0
#define LV_USE_TEXTAREA   0
#define LV_USE_TABLE      0

/* Layouts: only flex is needed for simple horizontal/vertical arrangement */
#define LV_USE_FLEX 1
#define LV_USE_GRID 0

/* Themes: keep only basic to reduce size */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_BASIC   1

/* Others */
#define LV_USE_SNAPSHOT 0
#define LV_USE_MONKEY   0

/* Disable drawing/image codec extras not used */
#define LV_USE_PNG 0
#define LV_USE_BMP 0
#define LV_USE_JPG 0
#define LV_USE_SJPG 0
#define LV_USE_GIF 0
#define LV_USE_QRCODE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_TINY_TTF 0

/* File system drivers not required */
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_STDIO 0

/* Logging */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 1
#endif

/* Asserts */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/* Others */
#define LV_SPRINTF_CUSTOM 0
#define LV_USE_USER_DATA 1

#endif /*LV_CONF_H*/