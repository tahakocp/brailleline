/* blcli "text" [budget_mA]: push text through the device model and show what moved. */
#include "../src/brailleline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void utf8(uint8_t v) { unsigned cp = 0x2800 + v; printf("%c%c%c", 0xE0 | (cp >> 12), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63)); }
static void on_pulse(void *ctx, const bl_pulse *p, int n, uint32_t us) {
  int *slot = ctx; printf("slot %2d (%u us):", (*slot)++, us);
  for (int i = 0; i < n; i++) printf(" cell%d.dot%d%s", p[i].cell, p[i].dot + 1, p[i].up ? "^" : "v");
  printf("\n");
}
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: blcli \"text\" [budget_mA]\n"); return 2; }
  int budget = argc > 2 ? atoi(argv[2]) : 360;
  bl_dev d; bl_dev_init(&d, 40, (uint16_t)budget, 120, 3000);
  int slot = 0; d.hal = on_pulse; d.hal_ctx = &slot;
  uint8_t pl[BL_MAX_PAYLOAD], out[BL_MAX_PAYLOAD + 5]; size_t n = strlen(argv[1]); if (n > 40) n = 40;
  pl[0] = 0; for (size_t i = 0; i < n; i++) pl[1 + i] = bl_ascii_dots(argv[1][i]);
  bl_dev_handle(&d, BL_CMD_SET_LINE, pl, n + 1, out);
  for (size_t i = 0; i < n; i++) utf8(pl[1 + i]);
  printf("\n%d slots, %d dot pulses, %u us total at %d mA (%d at once)\n", out[3] << 8 | out[4], out[5] << 8 | out[6],
         (unsigned)(out[7] << 24 | out[8] << 16 | out[9] << 8 | out[10]), budget, bl_concurrent(budget, 120));
  return 0;
}
