
#pragma once

#define BOARD_44KEY
//#define BOARD_OPENLUAT_ESP32C3
//#define BOARD_CUSTOM

#if defined(BOARD_44KEY)
#define PIN_LED (2)
#define LED_ACTIVE (0)
#define PIN_BTN_CONFIRM (9)
#elif defined(BOARD_OPENLUAT_ESP32C3)
#define PIN_LED (12)
#define LED_ACTIVE (1)
#define PIN_BTN_CONFIRM (9)
#elif defined(BOARD_CUSTOM)
// TODO: Define your custom board here
#else
#error "Please define your board!"
#endif
