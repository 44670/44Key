#include "../Firmware/main/bip39.h"
#include "../Firmware/main/crypto/memzero.c"
#include "../Firmware/main/crypto/sha2.c"
#include <stdio.h>
#include <string.h>
#include <stdint.h>


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

int main() {
    uint8_t key[16] = {0};
    const char* wordList[30];
    int wordCount = convertKeyToBip39(key, sizeof(key), wordList, sizeof(wordList) / sizeof(const char*));
    printf("wordCount: %d\n", wordCount);
    for (int i = 0; i < wordCount; i++) {
        printf("%s ", wordList[i]);
    }
    // Should be: bleak version runway tell hour unfold donkey defy digital abuse glide please omit much cement sea sweet tenant demise taste emerge inject cause link 
    return 0;
}