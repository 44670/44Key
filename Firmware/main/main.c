#include "ed25519.h"
#include "hal.h"
#include "memzero.h"
#include "sha2.h"
#include "bip39.h"

#define MAX_CMD_ARGS 8
const char *cmdArgs[MAX_CMD_ARGS];
char cmdBuf[4096];
uint8_t dataBuf[2048];
uint8_t pubKeyBuf[32];
uint8_t signResultBuf[256];

/* Secrets */
#define DEVICE_SECRET_USABLE_LEN (16)
uint8_t deviceSecretInfo[HAL_SECRET_INFO_SIZE];
uint8_t deviceSecretSeed[32];
uint8_t userSecretSeedBuf[32];
uint8_t secretKeyBuf[32];
/* End of secrets */

int isUserSeedSet = 0;

int decodeHexDigit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

int tryDecodeHexBuf(const char *hex, uint8_t *dst, size_t dstLen) {
  int dstPos = 0;
  memset(dst, 0, dstLen);
  while (*hex) {
    char c1 = *hex;
    char c2 = *(hex + 1);
    if (c2 == 0) {
      return -1;
    }
    hex += 2;
    int d1 = decodeHexDigit(c1);
    int d2 = decodeHexDigit(c2);
    if ((d1 < 0) || (d2 < 0)) {
      return -1;
    }
    if (dstPos >= dstLen) {
      return -1;
    }
    dst[dstPos] = (d1 << 4) | d2;
    dstPos++;
  }
  return dstPos;
}

void clearSecretKey() { memzero(secretKeyBuf, sizeof(secretKeyBuf)); }

void clearDeviceSecretSeed() {
  memzero(deviceSecretSeed, sizeof(deviceSecretSeed));
}

// deviceSecretSeed := SHA256(deviceSecretInfo[:1024] + "44KeyDeviceSecretSeed!")
int deriveDeviceSecretSeed() {
  HAL_ASSERT(HAL_SECRET_INFO_SIZE >= 1024);
  clearDeviceSecretSeed();
  HAL_ASSERT(halReadSecretInfo(deviceSecretInfo) == 0);
  if (deviceSecretInfo[HAL_SECRET_INFO_SIZE - 2] != 0x55 ||
      deviceSecretInfo[HAL_SECRET_INFO_SIZE - 1] != 0xAA) {
    return -1;
  }

  SHA256_CTX ctx = {0};
  sha256_Init(&ctx);
  // Use first 1024 bytes of deviceSecretInfo to generate deviceSecretSeed
  sha256_Update(&ctx, deviceSecretInfo, 1024);
  memzero(deviceSecretInfo, HAL_SECRET_INFO_SIZE);
  const char *appendStr = "44KeyDeviceSecretSeed!";
  sha256_Update(&ctx, (uint8_t*) appendStr, strlen(appendStr));
  sha256_Final(&ctx, deviceSecretSeed);
  // Set last 128 bits of deviceSecretSeed to 0
  memzero(deviceSecretSeed + 16, 16);
  return 0;
}

// secretKeyWithUsage := SHA256(usage + userSecretSeed + usage +
// "44KeyGenerateSecretKeyForUsage!")
int deriveSecretKeyWithUsage(const char *usage) {
  SHA256_CTX ctx = {0};
  clearSecretKey();
  if (!isUserSeedSet) {
    return -1;
  }
  if (strlen(usage) < 5) {
    return -1;
  }
  sha256_Init(&ctx);
  sha256_Update(&ctx, (uint8_t *)usage, strlen(usage));
  sha256_Update(&ctx, userSecretSeedBuf, sizeof(userSecretSeedBuf));
  sha256_Update(&ctx, (uint8_t *)usage, strlen(usage));
  const char *appendStr = "44KeyGenerateSecretKeyForUsage!";
  sha256_Update(&ctx, (uint8_t *)appendStr, strlen(appendStr));
  sha256_Final(&ctx, secretKeyBuf);
  return 0;
}

// Prepare secret key by usage
int cmdPrepareSecretKey(const char *usage, const char *ensureUsagePrefix) {
  if (!isUserSeedSet) {
    halUartWriteStr("+ERR,user seed not set\r\n");
    return -1;
  }
  if (strlen(usage) < 5) {
    halUartWriteStr("+ERR,usage too short\n");
    return -1;
  }
  if (strncmp(usage, ensureUsagePrefix, strlen(ensureUsagePrefix)) != 0) {
    halUartWriteStr("+ERR,usage prefix mismatch\n");
    return -1;
  }
  int ret = deriveSecretKeyWithUsage(usage);
  if (ret < 0) {
    halUartWriteStr("+ERR,failed\n");
    return -1;
  }
  return 0;
}

// Get public key by usage
void cmdPubKey() {
  if (cmdPrepareSecretKey(cmdArgs[1], "ed25519-ssh-") != 0) {
    return;
  }
  ed25519_publickey(secretKeyBuf, pubKeyBuf);
  clearSecretKey();
  halUartWriteStr("+OK,");
  halUartWriteHexBuf(pubKeyBuf, sizeof(pubKeyBuf));
  halUartWriteStr("\n");
}

// Sign data with secret key by usage
void cmdSign() {
  if (halRequestUserConsent("Sign") != 0) {
    halUartWriteStr("+ERR,aborted by user\n");
    return;
  }
  if (cmdPrepareSecretKey(cmdArgs[1], "ed25519-ssh-") != 0) {
    return;
  }
  ed25519_publickey(secretKeyBuf, pubKeyBuf);
  int dataLen = tryDecodeHexBuf(cmdArgs[2], dataBuf, sizeof(dataBuf));
  if (dataLen < 8) {
    halUartWriteStr("+ERR,invalid data\n");
    return;
  }
  ed25519_sign(dataBuf, dataLen, secretKeyBuf, pubKeyBuf, signResultBuf);
  clearSecretKey();
  halUartWriteStr("+OK,");
  halUartWriteHexBuf(signResultBuf, sizeof(ed25519_signature));
  halUartWriteStr("\n");
}



int convertKeyToBip39(const uint8_t *key, size_t keyLen, const char *wordList[], size_t wordListLen) {
    if (keyLen % 4 != 0) {
        return -1;
    }
    int keyBits = keyLen * 8;
    int checkSumBits = keyBits / 32;
    if (checkSumBits > 8) {
        return -1;
    }
    int wordCount = (keyBits + checkSumBits) / 11;
    if (wordListLen < wordCount) {
        return -1;
    }
    uint32_t bitPool = 0;
    int bitCnt = 0;
    SHA256_CTX ctx = {0};
    uint8_t hashResult[32] = {0};
    sha256_Init(&ctx);
    sha256_Update(&ctx, key, keyLen);
    sha256_Final(&ctx, (uint8_t*) hashResult);
    int keyPos = 0;
    for (int i = 0; i < wordCount; i++) {
        while (bitCnt < 11) {
            if (keyPos >= keyLen) {
                bitPool <<= 8;
                bitPool |= hashResult[0];
                bitCnt += 8;
            }
            bitPool <<= 8;
            bitPool |= key[keyPos];
            keyPos += 1;
            bitCnt += 8;
        }
        uint32_t wordIdx = (bitPool >> (bitCnt - 11)) & 0x7FF;
        bitCnt -= 11;
        wordList[i] = bip39Words[wordIdx];
    }

    return wordCount;
}

#ifdef HAVE_LCD
void showRecoverySeed() {
  halFBFill(COLOR_BLACK);
  halFBDrawStr(0, 0, "Device secret seed generated.\nYour ONLY chance to backup your device secret seed is now.\nPress the key to continue.",COLOR_WHITE);
  halLcdUpdateFB();
  halWaitKey();
  deriveDeviceSecretSeed();
  const char* worldList[30];
  int wordCount = convertKeyToBip39(deviceSecretSeed, DEVICE_SECRET_USABLE_LEN, worldList, 30);
  HAL_ASSERT(wordCount > 0);
  int currentWord = 0;
  while(1) {
    halFBFill(COLOR_BLACK);
    halFBPrintf(0, 0, COLOR_WHITE, "Secret seed(%d/%d):\n %s", currentWord + 1, wordCount, worldList[currentWord]);
    halFBDrawStr(0, 32, "Write down on the paper, then press the key to continue.", COLOR_WHITE);
    halLcdUpdateFB();
    halWaitKey();
    currentWord += 1;
    if (currentWord >= wordCount) {
      currentWord = 0;
    }
  }
  

}
#endif
// Format device: regenerate deviceSecretInfo
void cmdFormat() {
  if (halRequestUserConsent("Format device.") != 0) {
    halUartWriteStr("+ERR,aborted by user\n");
    return;
  }
  if (isUserSeedSet) {
    halUartWriteStr("+ERR,user seed already set\n");
    return;
  }
  int dataLen = tryDecodeHexBuf(cmdArgs[1], dataBuf, sizeof(dataBuf));
  if (dataLen != 32) {
    halUartWriteStr("+ERR,invalid data\n");
    return;
  }

  memzero(deviceSecretInfo, HAL_SECRET_INFO_SIZE);

  for (int i = 0; i < 32; i++) {
    deviceSecretInfo[i] = dataBuf[i];
  }
  for (int loop = 0; loop < 10; loop++) {
    for (int i = 0; i < HAL_SECRET_INFO_SIZE; i++) {
      deviceSecretInfo[i] ^= halRandomU32() & 0xFF;
    }
    halDelayMs(1000);
  }
  deviceSecretInfo[HAL_SECRET_INFO_SIZE - 2] = 0x55;
  deviceSecretInfo[HAL_SECRET_INFO_SIZE - 1] = 0xAA;

  HAL_ASSERT(halProgramSecretInfo(deviceSecretInfo) == 0);
  halUartWriteStr("+OK\n");
  #ifdef HAVE_LCD
  showRecoverySeed();
  #endif
  // Reboot
  esp_restart();
}


// userSecretSeed := SHA256(pwdHash + deviceSecretSeed + pwdHash +
// "44KeyGenerateUserSecretSeedByPassword!")
void cmdUserSeed() {
  if (halRequestUserConsent("Login with password.") != 0) {
    halUartWriteStr("+ERR,aborted by user\n");
    return;
  }
  if (isUserSeedSet) {
    halUartWriteStr("+ERR,user seed already set\n");
    return;
  }
  int dataLen = tryDecodeHexBuf(cmdArgs[1], dataBuf, sizeof(dataBuf));
  if (dataLen != 32) {
    halUartWriteStr("+ERR,invalid data\n");
    return;
  }
  if (deriveDeviceSecretSeed() < 0) {
    halUartWriteStr("+ERR,not formatted\n");
    return;
  }
  halLockSecretInfo();
  SHA256_CTX ctx = {0};
  sha256_Init(&ctx);
  sha256_Update(&ctx, dataBuf, 32);
  sha256_Update(&ctx, deviceSecretSeed, DEVICE_SECRET_USABLE_LEN);
  clearDeviceSecretSeed();
  sha256_Update(&ctx, dataBuf, 32);
  const char *appendStr = "44KeyGenerateUserSecretSeedByPassword!";
  sha256_Update(&ctx, (uint8_t *)appendStr, strlen(appendStr));
  sha256_Final(&ctx, userSecretSeedBuf);
  isUserSeedSet = 1;
  halUartWriteStr("+OK\n");
}

// webPwd := SHA256(secretKeyWithUsage(usage) + "44KeyGenerateWebPassword!")
void cmdWebPwd() {
  char prompt[128];
  memset(prompt, 0, sizeof(prompt));
  snprintf(prompt, sizeof(prompt), "Generate password: %s", cmdArgs[1]);

  if (halRequestUserConsent(prompt) != 0) {
    halUartWriteStr("+ERR,aborted by user\n");
    return;
  }
  if (cmdPrepareSecretKey(cmdArgs[1], "webpwd-") != 0) {
    return;
  }
  SHA256_CTX ctx = {0};
  sha256_Init(&ctx);
  sha256_Update(&ctx, secretKeyBuf, sizeof(secretKeyBuf));
  clearSecretKey();
  const char *appendStr = "44KeyGenerateWebPassword!";
  sha256_Update(&ctx, (uint8_t *)appendStr, strlen(appendStr));
  sha256_Final(&ctx, dataBuf);
  halUartWriteStr("+OK,");
  halUartWriteHexBuf(dataBuf, 32);
  halUartWriteStr("\n");
  return;
}

int app_main(void) {
  isUserSeedSet = 0;
  halInit();
  halUartClearInput();
  halShowMsg(isUserSeedSet ? "Connected." : "Waiting for connection..." );

  while (1) {
    memset(cmdBuf, 0, sizeof(cmdBuf));
    for (int i = 0; i < MAX_CMD_ARGS; i++) {
      cmdArgs[i] = "";
    }
    int ret = halUartReadLine(cmdBuf, sizeof(cmdBuf));
    if (ret != 0) {
      continue;
    }
    if (strlen(cmdBuf) < 2) {
      continue;
    }
    // Split cmdBuf by ',', and store the result in cmdArgs
    int argCount = 1;
    int pos = 0;
    cmdArgs[0] = cmdBuf;
    while ((argCount < MAX_CMD_ARGS) && (cmdBuf[pos])) {
      if (cmdBuf[pos] == ',') {
        cmdBuf[pos] = 0;
        cmdArgs[argCount] = cmdBuf + pos + 1;
        argCount++;
      }
      pos++;
    }
    if (strcmp(cmdArgs[0], "+PING") == 0) {
      halUartWriteStr("+PONG\n");
    } else if (strcmp(cmdArgs[0], "+USERSEED") == 0) {
      cmdUserSeed();
    } else if (strcmp(cmdArgs[0], "+PUBKEY") == 0) {
      cmdPubKey();
    } else if (strcmp(cmdArgs[0], "+SIGN") == 0) {
      cmdSign();
    } else if (strcmp(cmdArgs[0], "+FORMAT") == 0) {
      cmdFormat();
    } else if (strcmp(cmdArgs[0], "+WEBPWD") == 0) {
      cmdWebPwd();
    } else {
      halUartWriteStr("+ERR,unknown command\n");
    }
    halShowMsg(isUserSeedSet ? "Connected." : "Waiting for connection..." );
  }
  return 0;
}