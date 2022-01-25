#include <stdint.h>
#include <string.h>

void memzero(void *const pnt, const size_t len) {

  volatile unsigned char *volatile pnt_ = (volatile unsigned char *volatile)pnt;
  size_t i = (size_t)0U;

  while (i < len) {
    pnt_[i++] = 0U;
  }
}
