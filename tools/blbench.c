/* Replays a text file as successive 40-cell lines and reports slots and time. */
#include "../src/brailleline.h"
#include <stdio.h>
#include <string.h>
#define COLS 40
int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: blbench file.txt\n"); return 2; }
  FILE *f = fopen(argv[1], "r"); if (!f) { perror("open"); return 2; }
  static char text[1 << 16]; size_t n = fread(text, 1, sizeof text - 1, f); text[n] = 0; fclose(f);
  for (size_t i = 0; i < n; i++) if (text[i] == '\n') text[i] = ' ';
  /* word wrap into lines of COLS */
  static uint8_t lines[512][COLS]; int nl = 0; size_t pos = 0;
  while (pos < n && nl < 512) {
    size_t end = pos + COLS; if (end >= n) end = n; else { while (end > pos && text[end] != ' ') end--; if (end == pos) end = pos + COLS; }
    memset(lines[nl], 0, COLS);
    for (size_t i = pos, c = 0; i < end && c < COLS; i++, c++) lines[nl][c] = bl_ascii_dots(text[i]);
    nl++; pos = end; while (pos < n && text[pos] == ' ') pos++;
  }
  int pulse_ma = 120, pulse_us = 3000;
  printf("%d lines of %d cells, pulse %d mA for %d us per dot\n\n", nl, COLS, pulse_ma, pulse_us);
  printf("| supply budget | concurrent pulses | avg dots moved per line | avg slots, fastest | avg refresh ms, fastest | avg refresh ms, reading order | worst line ms | rewrite-everything ms |\n|---|---|---|---|---|---|---|---|\n");
  int budgets[] = {120, 240, 500, 900};
  for (int b = 0; b < 4; b++) {
    int conc = bl_concurrent(budgets[b], pulse_ma);
    double dots = 0, sl = 0, slr = 0; int worst = 0;
    uint8_t cur[COLS] = {0};
    static bl_pulse out[COLS * 8]; static uint16_t st[COLS * 8 + 1];
    for (int l = 0; l < nl; l++) {
      int ns = bl_plan(cur, lines[l], COLS, conc, BL_SCHED_FASTEST, out, COLS * 8, st, COLS * 8);
      int nr = bl_plan(cur, lines[l], COLS, conc, BL_SCHED_READING, out, COLS * 8, st, COLS * 8);
      dots += st[ns]; sl += ns; slr += nr; if (ns > worst) worst = ns;
      memcpy(cur, lines[l], COLS);
    }
    int naive = (COLS * 6 + conc - 1) / conc; /* push every one of the 6 dots in every cell */
    printf("| %d mA | %d | %.1f | %.1f | %.0f | %.0f | %d | %d |\n", budgets[b], conc, dots / nl, sl / nl,
           sl / nl * pulse_us / 1000.0, slr / nl * pulse_us / 1000.0, worst * pulse_us / 1000, naive * pulse_us / 1000);
  }
  return 0;
}
