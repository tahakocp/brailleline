/* Emits JSON test vectors from the C core so the JavaScript port can be checked against it. */
#include "../src/brailleline.h"
#include <stdio.h>
#include <string.h>
static uint32_t rs = 88172645u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static void hex(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) printf("%02x", p[i]); }
int main(void) {
  printf("{\n\"crc\":[");
  for (int i = 0; i < 40; i++) {
    uint8_t b[24]; size_t n = rnd() % 24; for (size_t k = 0; k < n; k++) b[k] = (uint8_t)rnd();
    printf("%s{\"data\":\"", i ? "," : ""); hex(b, n); printf("\",\"crc\":%u}", bl_crc16(b, n));
  }
  printf("],\n\"frames\":[");
  for (int i = 0; i < 40; i++) {
    uint8_t b[40], f[50]; size_t n = rnd() % 30; uint8_t cmd = (uint8_t)(1 + rnd() % 4);
    for (size_t k = 0; k < n; k++) b[k] = (uint8_t)rnd();
    size_t m = bl_encode(cmd, b, n, f);
    printf("%s{\"cmd\":%u,\"payload\":\"", i ? "," : "", cmd); hex(b, n); printf("\",\"frame\":\""); hex(f, m); printf("\"}");
  }
  printf("],\n\"ascii\":\"");
  for (int c = 32; c < 127; c++) printf("%02x", bl_ascii_dots((char)c));
  printf("\",\n\"plans\":[");
  for (int i = 0; i < 300; i++) {
    int n = 1 + rnd() % 40, conc = 1 + rnd() % 6, mode = rnd() & 1;
    uint8_t cur[BL_MAX_CELLS], tgt[BL_MAX_CELLS];
    for (int k = 0; k < n; k++) { cur[k] = (uint8_t)rnd(); tgt[k] = (rnd() & 1) ? cur[k] : (uint8_t)rnd(); }
    static bl_pulse out[BL_MAX_CELLS * 8]; static uint16_t st[BL_MAX_CELLS * 8 + 1];
    int ns = bl_plan(cur, tgt, n, conc, mode, out, BL_MAX_CELLS * 8, st, BL_MAX_CELLS * 8);
    printf("%s{\"conc\":%d,\"mode\":%d,\"cur\":\"", i ? "," : "", conc, mode); hex(cur, n);
    printf("\",\"tgt\":\""); hex(tgt, n); printf("\",\"slots\":%d,\"pulses\":[", ns);
    for (int k = 0; k < st[ns]; k++) printf("%s[%u,%u,%u]", k ? "," : "", out[k].cell, out[k].dot, out[k].up);
    printf("],\"starts\":["); for (int s = 0; s <= ns; s++) printf("%s%u", s ? "," : "", st[s]); printf("]}");
  }
  printf("]\n}\n");
  return 0;
}
