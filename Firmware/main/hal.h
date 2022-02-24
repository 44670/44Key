// HAL definitions for ESP-IDF (esp32-c3)
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

// Include esp-idf headers, uart, gpio, efuse, spi-flash
#include "bootloader_random.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_secure_boot.h"
#include "esp_system.h"

// Include FreeRTOS headers
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define HAL_SECRET_INFO_SIZE (4096)

void halDelayMs(uint32_t ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

const esp_partition_t *halSecretPartition;

void halAssertFailed(const char *file, int line, const char *msg) {
  printf("+ERR,ASSERT: %s:%d %s\n", file, line, msg);
  printf("HALT\n");
  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

#define HAL_ASSERT(cond) \
  (!!(cond) ? (void)0 : halAssertFailed(__FILENAME__, __LINE__, #cond))

// Ensure that the random number generator is ready
// TODO: panic if RNG is broken
void halEnsureRandomReady() { bootloader_random_enable(); }

int halIsDeviceSecure = 0;

void halDoSecureCheck() {
  esp_flash_enc_mode_t mode = esp_get_flash_encryption_mode();
  if (mode != ESP_FLASH_ENC_MODE_RELEASE) {
    ESP_LOGW("HAL", "Not Secure: Flash encryption mode is %d", mode);
    return;
  }
  if (!esp_secure_boot_enabled()) {
    ESP_LOGW("HAL", "Not Secure: Secure boot not enabled");
    return;
  }
  if (!esp_efuse_read_field_bit(ESP_EFUSE_DIS_PAD_JTAG)) {
    ESP_LOGW("HAL", "Not Secure: JTAG is enabled");
    return;
  }
  if (!esp_efuse_read_field_bit(ESP_EFUSE_DIS_USB_JTAG)) {
    ESP_LOGW("HAL", "Not Secure: USB JTAG is enabled");
    return;
  }
  halIsDeviceSecure = 1;
}

uint32_t halRandomU32() { return esp_random(); }

#ifdef HAVE_LCD
#include "vgafont8.h"
#define COLOR_WHITE (0xffff)
#define COLOR_BLACK (0)
#define COLOR_RED (0x00F8)
#define COLOR_GREEN (0xE007)
#define COLOR_BLUE (0x1F00)

uint16_t halLcdFB[LCD_WIDTH * LCD_HEIGHT];
spi_device_handle_t halLcdDev;

void halLcdWrite(uint8_t *buf, int len, int isCmd) {
  if (len <= 0) {
    return;
  }
  gpio_set_level(PIN_LCD_DC, isCmd ? 0 : 1);
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8 * len;
  t.tx_buffer = buf;
  ESP_ERROR_CHECK(spi_device_transmit(halLcdDev, &t));
  gpio_set_level(PIN_LCD_DC, 1);
}

void halLcdCmd8(uint8_t cmd) { halLcdWrite(&cmd, 1, 1); }

void halLcdDat16(uint16_t dat) {
  uint8_t buf[2] = {dat >> 8, dat & 0xff};
  halLcdWrite(buf, 2, 0);
}

void halLcdDat8(uint8_t dat) { halLcdWrite(&dat, 1, 0); }

void halLcdSetWindow(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
  if (LCD_ROTATION == 0) {
    halLcdCmd8(0x2a);
    halLcdDat16(x1 + 26);
    halLcdDat16(x2 + 26);
    halLcdCmd8(0x2b);
    halLcdDat16(y1 + 1);
    halLcdDat16(y2 + 1);
    halLcdCmd8(0x2c);
  } else if (LCD_ROTATION == 1) {
    halLcdCmd8(0x2a);
    halLcdDat16(x1 + 26);
    halLcdDat16(x2 + 26);
    halLcdCmd8(0x2b);
    halLcdDat16(y1 + 1);
    halLcdDat16(y2 + 1);
    halLcdCmd8(0x2c);
  } else if (LCD_ROTATION == 2) {
    halLcdCmd8(0x2a);
    halLcdDat16(x1 + 1);
    halLcdDat16(x2 + 1);
    halLcdCmd8(0x2b);
    halLcdDat16(y1 + 26);
    halLcdDat16(y2 + 26);
    halLcdCmd8(0x2c);
  } else {
    halLcdCmd8(0x2a);
    halLcdDat16(x1 + 1);
    halLcdDat16(x2 + 1);
    halLcdCmd8(0x2b);
    halLcdDat16(y1 + 26);
    halLcdDat16(y2 + 26);
    halLcdCmd8(0x2c);
  }
}

void halFBFill(uint16_t color) {
  for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
    halLcdFB[i] = color;
  }
}

static inline void halFBSetPixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
    return;
  }
  halLcdFB[y * LCD_WIDTH + x] = color;
}

static inline uint16_t halFBGetPixel(int x, int y) {
  if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
    return 0;
  }
  return halLcdFB[y * LCD_WIDTH + x];
}

void halFBDrawStr(int x, int y, const char *str, uint16_t color) {
  int pos = 0;
  while (str[pos]) {
    char ch = str[pos];
    pos++;
    if (ch == '\n') {
      y += 8;
      x = 0;
      continue;
    }
    if (x + 8 > LCD_WIDTH) {
      y += 8;
      x = 0;
    }
    if (y + 8 > LCD_HEIGHT) {
      break;
    }
    for (int i = 0; i < 8; i++) {
      uint8_t t = vgafont8[ch * 8 + i];
      for (int j = 0; j < 8; j++) {
        if (t & (1 << (7 - j))) {
          halFBSetPixel(x + j, y + i, color);
        }
      }
    }
    x += 8;
  }
}

void halLcdPrintf(int x, int y, uint16_t color, const char *fmt, ...) {
  char buf[256];
  memset(buf, 0, sizeof(buf));
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  halFBDrawStr(x, y, buf, color);
}

void halLcdUpdateFB() {
  if (!halIsDeviceSecure) {
    halFBDrawStr(0, LCD_HEIGHT - 8, "NOT SECURE!", COLOR_RED);
  }
  halLcdSetWindow(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

  halLcdWrite((uint8_t *)(halLcdFB), LCD_WIDTH * LCD_HEIGHT * 2, 0);
}

void halLcdInit() {
  gpio_set_direction(PIN_LCD_RSTN, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LCD_CS, 1);
  gpio_set_level(PIN_LCD_RSTN, 0);
  halDelayMs(100);
  gpio_set_level(PIN_LCD_RSTN, 1);
  gpio_set_direction(PIN_LCD_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LCD_DC, 1);
  gpio_set_direction(PIN_LCD_DC, GPIO_MODE_OUTPUT);
  spi_bus_config_t buscfg = {
      .miso_io_num = -1,
      .mosi_io_num = PIN_LCD_MOSI,
      .sclk_io_num = PIN_LCD_SCLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 30000,
  };
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
  ESP_LOGI("HAL", "SPI2 initialized");
  spi_device_interface_config_t devcfg = {.clock_speed_hz = 5000000,
                                          .mode = 0,
                                          .spics_io_num = PIN_LCD_CS,
                                          .queue_size = 7};
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &halLcdDev));
  ESP_LOGI("HAL", "Device Added");

  halLcdCmd8(0x11);  // Sleep out
  halDelayMs(120);   // Delay 120ms
  halLcdCmd8(0xB1);  // Normal mode
  halLcdDat8(0x05);
  halLcdDat8(0x3C);
  halLcdDat8(0x3C);
  halLcdCmd8(0xB2);  // Idle mode
  halLcdDat8(0x05);
  halLcdDat8(0x3C);
  halLcdDat8(0x3C);
  halLcdCmd8(0xB3);  // Partial mode
  halLcdDat8(0x05);
  halLcdDat8(0x3C);
  halLcdDat8(0x3C);
  halLcdDat8(0x05);
  halLcdDat8(0x3C);
  halLcdDat8(0x3C);
  halLcdCmd8(0xB4);  // Dot inversion
  halLcdDat8(0x03);
  halLcdCmd8(0xC0);  // AVDD GVDD
  halLcdDat8(0xAB);
  halLcdDat8(0x0B);
  halLcdDat8(0x04);
  halLcdCmd8(0xC1);  // VGH VGL
  halLcdDat8(0xC5);  // C0
  halLcdCmd8(0xC2);  // Normal Mode
  halLcdDat8(0x0D);
  halLcdDat8(0x00);
  halLcdCmd8(0xC3);  // Idle
  halLcdDat8(0x8D);
  halLcdDat8(0x6A);
  halLcdCmd8(0xC4);  // Partial+Full
  halLcdDat8(0x8D);
  halLcdDat8(0xEE);
  halLcdCmd8(0xC5);  // VCOM
  halLcdDat8(0x0F);
  halLcdCmd8(0xE0);  // positive gamma
  halLcdDat8(0x07);
  halLcdDat8(0x0E);
  halLcdDat8(0x08);
  halLcdDat8(0x07);
  halLcdDat8(0x10);
  halLcdDat8(0x07);
  halLcdDat8(0x02);
  halLcdDat8(0x07);
  halLcdDat8(0x09);
  halLcdDat8(0x0F);
  halLcdDat8(0x25);
  halLcdDat8(0x36);
  halLcdDat8(0x00);
  halLcdDat8(0x08);
  halLcdDat8(0x04);
  halLcdDat8(0x10);
  halLcdCmd8(0xE1);  // negative gamma
  halLcdDat8(0x0A);
  halLcdDat8(0x0D);
  halLcdDat8(0x08);
  halLcdDat8(0x07);
  halLcdDat8(0x0F);
  halLcdDat8(0x07);
  halLcdDat8(0x02);
  halLcdDat8(0x07);
  halLcdDat8(0x09);
  halLcdDat8(0x0F);
  halLcdDat8(0x25);
  halLcdDat8(0x35);
  halLcdDat8(0x00);
  halLcdDat8(0x09);
  halLcdDat8(0x04);
  halLcdDat8(0x10);

  halLcdCmd8(0xFC);
  halLcdDat8(0x80);

  halLcdCmd8(0x3A);
  halLcdDat8(0x05);
  halLcdCmd8(0x36);

  if (LCD_ROTATION == 0)
    halLcdDat8(0x08);
  else if (LCD_ROTATION == 1)
    halLcdDat8(0xC8);
  else if (LCD_ROTATION == 2)
    halLcdDat8(0x78);
  else
    halLcdDat8(0xA8);
  halLcdCmd8(0x21);  // Display inversion
  halLcdCmd8(0x29);  // Display on
  halLcdCmd8(0x2A);  // Set Column Address
  halLcdDat8(0x00);
  halLcdDat8(0x1A);  // 26
  halLcdDat8(0x00);
  halLcdDat8(0x69);  // 105
  halLcdCmd8(0x2B);  // Set Page Address
  halLcdDat8(0x00);
  halLcdDat8(0x01);  // 1
  halLcdDat8(0x00);
  halLcdDat8(0xA0);  // 160
  halLcdCmd8(0x2C);
  halFBFill(COLOR_BLACK);
  halFBDrawStr(0, 0, "HELLO", COLOR_WHITE);
  halLcdUpdateFB();
}

#endif

void halInit() {
  uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_APB,
  };

  ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 4096, 4096, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));
  // Open secret partition
  halSecretPartition = esp_partition_find_first(
      ESP_PARTITION_TYPE_ANY, ESP_PARTITION_TYPE_ANY, "secret_data");
  HAL_ASSERT(halSecretPartition != NULL);
  halEnsureRandomReady();
#ifdef PIN_LED
  gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_LED, !LED_ACTIVE);
#endif
#ifdef PIN_BTN_CONFIRM
  gpio_set_direction(PIN_BTN_CONFIRM, GPIO_MODE_INPUT);
  gpio_set_pull_mode(PIN_BTN_CONFIRM, GPIO_PULLUP_ONLY);
  gpio_set_level(PIN_BTN_CONFIRM, 1);
#endif
  halDoSecureCheck();
#ifdef HAVE_LCD
  halLcdInit();
#endif
}

void halLockChip() {
  // TODO: Lock chip
}

int halGpioRead(int pin) { return gpio_get_level(pin); }

void halUartClearInput() {
  uint8_t buf[128];
  while (1) {
    int len =
        uart_read_bytes(UART_NUM_0, buf, sizeof(buf), 200 / portTICK_PERIOD_MS);
    if (len == 0) {
      break;
    }
  }
}

int halUartReadLine(char *buffer, size_t max_length) {
  memset(buffer, 0, max_length);
  char startByte = 0;
  int len = 0;
  while (1) {
    len = uart_read_bytes(UART_NUM_0, &startByte, 1, 100 / portTICK_PERIOD_MS);
    if (len == 1 && startByte == '+') {
      break;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  buffer[0] = 0;
  int pos = 1;
  while (1) {
    uint8_t byte;
    if (pos + 2 >= max_length) {
      return -1;
    }
    len = uart_read_bytes(UART_NUM_0, &byte, 1, 2000 / portTICK_PERIOD_MS);
    if (len <= 0) {
      return -1;
    }
    buffer[pos] = byte;
    pos += 1;
    if (byte == '\n') {
      break;
    }
  }
  buffer[0] = startByte;
  buffer[pos - 1] = 0;
  if (pos >= 2) {
    if (buffer[pos - 2] == '\r') {
      buffer[pos - 2] = 0;
    }
  }
  return 0;
}

int halUartWriteStr(const char *buffer) {
  int len = strlen(buffer);
  uart_write_bytes(UART_NUM_0, buffer, len);
  return 0;
}

int halUartWriteHexBuf(const uint8_t *buffer, size_t length) {
  const char *hex = "0123456789abcdef";
  char buf[8];
  for (int i = 0; i < length; i++) {
    uint8_t byte = buffer[i];
    buf[0] = hex[byte >> 4];
    buf[1] = hex[byte & 0x0f];
    uart_write_bytes(UART_NUM_0, buf, 2);
  }
  return 0;
}

int halReadSecretInfo(uint8_t *dst) {
  memset(dst, 0, HAL_SECRET_INFO_SIZE);
  if (halSecretPartition == NULL) {
    return -1;
  }
  esp_err_t err =
      esp_partition_read(halSecretPartition, 0, dst, HAL_SECRET_INFO_SIZE);
  if (err != ESP_OK) {
    return -1;
  }
  return 0;
}

int halProgramSecretInfo(const uint8_t *src) {
  esp_err_t err = esp_partition_erase_range(halSecretPartition, 0,
                                            halSecretPartition->size);
  if (err != ESP_OK) {
    printf("Failed to erase secret partition");
    return -1;
  }
  err = esp_partition_write(halSecretPartition, 0, src, HAL_SECRET_INFO_SIZE);
  if (err != ESP_OK) {
    printf("Failed to write secret partition");
    return -1;
  }
  return 0;
}

int halLockSecretInfo() {
  // TODO: disallow the secret info from being read until reset (if possible on
  // current platform)
  return 0;
}

void halShowMsg(const char *msg) {
#ifdef HAVE_LCD
  halFBFill(COLOR_BLACK);
  halLcdPrintf(0, 0, COLOR_GREEN, "44Key");
  halLcdPrintf(0, 8, COLOR_WHITE, msg);
  halLcdUpdateFB();
#endif
}

int halRequestUserConsent(const char *prompt) {
  int ret = -1;
#ifdef PIN_BTN_CONFIRM
#ifdef PIN_LED
  gpio_set_level(PIN_LED, LED_ACTIVE);
#endif
#ifdef HAVE_LCD
  halFBFill(COLOR_BLACK);
  halFBDrawStr(0, 0, "Confirm?", COLOR_RED);
  halFBDrawStr(0, 8, prompt, COLOR_WHITE);
  halLcdUpdateFB();
#endif
  // Wait until button is not pressed
  while (gpio_get_level(PIN_BTN_CONFIRM) == 0) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  int i = 0;
  for (i = 0; i < 300; i++) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    // Is button pressed?
    if (gpio_get_level(PIN_BTN_CONFIRM) == 0) {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      // Is button still pressed?
      if (gpio_get_level(PIN_BTN_CONFIRM) == 0) {
        ret = 0;
        break;
      }
    }
  }
#ifdef PIN_LED
  gpio_set_level(PIN_LED, !LED_ACTIVE);
#endif
#ifdef HAVE_LCD
  halFBFill(COLOR_BLACK);
  halFBDrawStr(0, 0, (ret == 0) ? "Confirmed\nPlease wait..." : "Cancelled",
               COLOR_WHITE);
  halLcdUpdateFB();
  if (ret != 0) {
    halDelayMs(1000);
  }
#endif
  return ret;
#else
  return 0;
#endif
}