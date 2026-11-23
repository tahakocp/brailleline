#include "../src/brailleline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static uint32_t rs = 2463534242u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static void test_crc(void) {
  CHECK(bl_crc16((const uint8_t *)"123456789", 9) == 0x29B1); /* CRC-16/CCITT-FALSE check value */
}

static void test_ascii(void) {
  CHECK(bl_ascii_dots('a') == 1);
  CHECK(bl_ascii_dots('b') == 3);
  CHECK(bl_ascii_dots('h') == 19);   /* dots 1,2,5 */
  CHECK(bl_ascii_dots('z') == 53);   /* dots 1,3,5,6 */
  CHECK(bl_ascii_dots('w') == 58);   /* dots 2,4,5,6 */
  CHECK(bl_ascii_dots('#') == 60);   /* number sign: dots 3,4,5,6 */
}

static void test_plan_optimal(void) {
  /* the schedule must hit max(largest per-cell backlog, ceil(total / concurrent)) */
  int worst_gap = 0;
  for (int iter = 0; iter < 20000; iter++) {
    int n = 1 + rnd() % 40, conc = 1 + rnd() % 8;
    uint8_t cur[BL_MAX_CELLS], tgt[BL_MAX_CELLS];
    int total = 0, maxd = 0;
    for (int i = 0; i < n; i++) {
      cur[i] = (uint8_t)rnd(); tgt[i] = (rnd() & 3) ? cur[i] ^ (uint8_t)(rnd() & rnd()) : (uint8_t)rnd();
      int d = bl_popcount8(cur[i] ^ tgt[i]); total += d; if (d > maxd) maxd = d;
    }
    static bl_pulse out[BL_MAX_CELLS * 8]; static uint16_t st[BL_MAX_CELLS * 8 + 1];
    int ns = bl_plan(cur, tgt, n, conc, BL_SCHED_FASTEST, out, BL_MAX_CELLS * 8, st, BL_MAX_CELLS * 8);
    int lb = (total + conc - 1) / conc; if (maxd > lb) lb = maxd;
    if (ns - lb > worst_gap) worst_gap = ns - lb;
    CHECK(ns == lb);
    /* apply it: every slot obeys the limits and the result equals the target */
    uint8_t sim[BL_MAX_CELLS]; memcpy(sim, cur, n);
    int ok = 1;
    for (int s = 0; s < ns; s++) {
      uint8_t seen[BL_MAX_CELLS] = {0};
      if (st[s + 1] - st[s] > conc) ok = 0;
      for (int k = st[s]; k < st[s + 1]; k++) {
        if (seen[out[k].cell]++) ok = 0;
        uint8_t bit = (uint8_t)(1u << out[k].dot);
        if (((sim[out[k].cell] & bit) != 0) == out[k].up) ok = 0; /* pulse must change the dot */
        sim[out[k].cell] ^= bit;
      }
    }
    CHECK(ok); CHECK(memcmp(sim, tgt, n) == 0); CHECK(st[ns] == total);
  }
  CHECK(worst_gap == 0);
}

static void test_reading_order(void) {
  uint8_t cur[10] = {0}, tgt[10]; for (int i = 0; i < 10; i++) tgt[i] = 0x3F;
  static bl_pulse out[80]; static uint16_t st[81];
  int ns = bl_plan(cur, tgt, 10, 2, BL_SCHED_READING, out, 80, st, 80);
  /* cells 0 and 1 are finished before cell 2 gets its first pulse */
  int last01 = -1, first2 = 999;
  for (int s = 0; s < ns; s++) for (int k = st[s]; k < st[s + 1]; k++) {
    if (out[k].cell < 2 && s > last01) last01 = s;
    if (out[k].cell == 2 && s < first2) first2 = s;
  }
  CHECK(last01 < first2);
  CHECK(ns == 30);
}

static void test_parser(void) {
  bl_parser p; bl_parser_init(&p);
  uint8_t f1[BL_MAX_PAYLOAD + 5], f2[BL_MAX_PAYLOAD + 5];
  uint8_t pl[] = {0, 1, 2, BL_SOF, 4};
  size_t n1 = bl_encode(BL_CMD_SET_LINE, pl, sizeof pl, f1);
  size_t n2 = bl_encode(BL_CMD_CLEAR, NULL, 0, f2);
  int got = 0;
  for (size_t i = 0; i < n1; i++) got += bl_parser_feed(&p, f1[i]);
  CHECK(got == 1 && p.cmd == BL_CMD_SET_LINE && p.len == 5 && memcmp(p.payload, pl, 5) == 0);
  /* garbage, a truncated frame, a bit-flipped frame, then a good one */
  uint8_t junk[] = {0x00, BL_SOF, 0x02, 0x7F, BL_SOF, BL_SOF, 0x55};
  got = 0;
  for (size_t i = 0; i < sizeof junk; i++) got += bl_parser_feed(&p, junk[i]);
  uint8_t bad[BL_MAX_PAYLOAD + 5]; memcpy(bad, f1, n1); bad[4] ^= 0x10;
  for (size_t i = 0; i < n1; i++) got += bl_parser_feed(&p, bad[i]);
  CHECK(got == 0);
  got += bl_parser_idle(&p); /* line went quiet */
  for (size_t i = 0; i < n2; i++) got += bl_parser_feed(&p, f2[i]);
  CHECK(got == 1 && p.cmd == BL_CMD_CLEAR);
  CHECK(p.crc_errors >= 1);
}

static void test_parser_fuzz(void) {
  /* random noise with valid frames mixed in: every valid frame must come out, in order */
  bl_parser p; bl_parser_init(&p);
  int sent = 0, got = 0;
  for (int iter = 0; iter < 3000; iter++) {
    int junk = rnd() % 12;
    for (int i = 0; i < junk; i++) { uint8_t b = (uint8_t)rnd(); if (b == BL_SOF && (rnd() & 1)) b = 0; got += bl_parser_feed(&p, b); }
    got += bl_parser_idle(&p); /* gap before the next real frame */
    uint8_t pl[BL_MAX_PAYLOAD], f[BL_MAX_PAYLOAD + 5]; size_t len = rnd() % 20;
    for (size_t i = 0; i < len; i++) pl[i] = (uint8_t)rnd();
    size_t n = bl_encode(BL_CMD_SET_LINE, pl, len, f); sent++;
    int here = 0;
    for (size_t i = 0; i < n; i++) here += bl_parser_feed(&p, f[i]);
    got += here;
  }
  
  printf("fuzz: %d of %d framed messages recovered through noise\n", got, sent);
  CHECK(got == sent);
}

static void test_dev(void) {
  bl_dev d; bl_dev_init(&d, 40, 400, 120, 3000); /* 3 concurrent pulses */
  uint8_t out[BL_MAX_PAYLOAD + 5];
  size_t n = bl_dev_handle(&d, BL_CMD_HELLO, NULL, 0, out);
  CHECK(n == 13 && out[1] == BL_RSP_INFO && out[3] == 40);
  uint8_t pl[1 + 5] = {0, 0x01, 0x03, 0x09, 0x19, 0x11}; /* h e l l o -> "hello" in dots */
  n = bl_dev_handle(&d, BL_CMD_SET_LINE, pl, sizeof pl, out);
  CHECK(out[1] == BL_RSP_ACK);
  int slots = out[3] << 8 | out[4], pulses = out[5] << 8 | out[6];
  CHECK(pulses == 1 + 2 + 2 + 3 + 2);
  CHECK(slots == 4); /* ceil(10/3) = 4, biggest cell backlog 3 */
  n = bl_dev_handle(&d, BL_CMD_SET_LINE, pl, sizeof pl, out); /* same text again: nothing to move */
  CHECK(out[1] == BL_RSP_ACK && out[3] == 0 && out[4] == 0 && out[6] == 0);
  uint8_t bad[3] = {39, 1, 2}; n = bl_dev_handle(&d, BL_CMD_SET_LINE, bad, 3, out);
  CHECK(out[1] == BL_RSP_NAK && out[3] == BL_ERR_RANGE);
  n = bl_dev_handle(&d, 0x77, NULL, 0, out); CHECK(out[1] == BL_RSP_NAK && out[3] == BL_ERR_CMD);
  (void)n;
}

int main(void) {
  test_crc(); test_ascii(); test_plan_optimal(); test_reading_order(); test_parser(); test_parser_fuzz(); test_dev();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
