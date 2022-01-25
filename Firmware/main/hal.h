// HAL definitions for ESP-IDF (esp32-c3)
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

// Include esp-idf headers, uart, gpio, efuse, spi-flash
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "bootloader_random.h"

// Include FreeRTOS headers
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define HAL_SECRET_INFO_SIZE (4096)

const esp_partition_t *halSecretPartition;

void halAssertFailed(const char *file, int line, const char *msg) {
  printf("+ERR,ASSERT: %s:%d %s\n", file, line, msg);
  printf("HALT\n");
  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}



#define HAL_ASSERT(cond) (!!(cond) ? (void)0 : halAssertFailed(__FILENAME__, __LINE__, #cond))

// Ensure that the random number generator is ready
// TODO: panic if RNG is broken
void halEnsureRandomReady() {
  bootloader_random_enable();
}

uint32_t halRandomU32() {
  return esp_random();
}

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
}

int halRequestUserConsent() {
  // TODO: notify user with led and wait for button press
  return 1;
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

int halReadSecretInfo(uint8_t* dst) {
  if (halSecretPartition == NULL) {
    return -1;
  }
  esp_err_t err = esp_partition_read(halSecretPartition, 0, dst,
                                     HAL_SECRET_INFO_SIZE);
  if (err != ESP_OK) {
    return -1;
  }
  return 0;
}

int halProgramSecretInfo(const uint8_t* src){
  esp_err_t err = esp_partition_erase_range(halSecretPartition, 0,
                                            halSecretPartition->size);
  if (err != ESP_OK) {
    printf("Failed to erase secret partition");
    return -1;
  }
  err =
      esp_partition_write(halSecretPartition, 0, src, HAL_SECRET_INFO_SIZE);
  if (err != ESP_OK) {
    printf("Failed to write secret partition");
    return -1;
  }
  return 0;
}
