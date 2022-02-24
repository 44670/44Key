
#pragma once

#define BOARD_44KEY
//#define BOARD_OPENLUAT_ESP32C3
//#define BOARD_CUSTOM

#if defined(BOARD_44KEY)
#define PIN_LED (2)
#define LED_ACTIVE (0)
#define PIN_BTN_CONFIRM (9)
#define HAVE_LCD (1)
#define PIN_LCD_MOSI (7)
#define PIN_LCD_SCLK (6)
#define PIN_LCD_RSTN (8)
#define PIN_LCD_CS (19)
#define PIN_LCD_DC (18)
#define LCD_WIDTH (160)
#define LCD_HEIGHT (80)
#define LCD_ROTATION (2)
#elif defined(BOARD_OPENLUAT_ESP32C3)
#define PIN_LED (12)
#define LED_ACTIVE (1)
#define PIN_BTN_CONFIRM (9)
#elif defined(BOARD_CUSTOM)
// TODO: Define your custom board here
#else
#error "Please define your board!"
#endif
