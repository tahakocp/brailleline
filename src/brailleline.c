#include "brailleline.h"
#include <string.h>

uint16_t bl_crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

size_t bl_encode(uint8_t cmd, const uint8_t *payload, size_t len, uint8_t *out) {
  out[0] = BL_SOF; out[1] = cmd; out[2] = (uint8_t)len;
  if (len) memcpy(out + 3, payload, len);
  uint16_t c = bl_crc16(out + 1, len + 2);
  out[3 + len] = (uint8_t)(c >> 8); out[4 + len] = (uint8_t)c;
  return len + 5;
}

void bl_parser_init(bl_parser *p) { memset(p, 0, sizeof *p); }

static int parser_step(bl_parser *p, uint8_t b) {
  if (p->have == 0) { if (b != BL_SOF) return 0; p->buf[p->have++] = b; return 0; }
  p->buf[p->have++] = b;
  if (p->have == 3 && p->buf[2] > BL_MAX_PAYLOAD) { /* bad length: resync */
    p->crc_errors++;
    uint8_t keep[3]; memcpy(keep, p->buf + 1, 2); p->have = 0;
    for (int i = 0; i < 2; i++) parser_step(p, keep[i]);
    return 0;
  }
  if (p->have >= 3 && p->have == (size_t)p->buf[2] + 5) {
    size_t len = p->buf[2];
    uint16_t want = bl_crc16(p->buf + 1, len + 2);
    uint16_t got = (uint16_t)(p->buf[3 + len] << 8 | p->buf[4 + len]);
    if (want == got) {
      p->cmd = p->buf[1]; p->len = (uint8_t)len; memcpy(p->payload, p->buf + 3, len);
      p->have = 0; p->frames++; return 1;
    }
    p->crc_errors++;
    /* the SOF was probably noise: replay everything after it */
    uint8_t tmp[BL_MAX_PAYLOAD + 5]; size_t n = p->have - 1;
    memcpy(tmp, p->buf + 1, n); p->have = 0;
    for (size_t i = 0; i < n; i++) if (parser_step(p, tmp[i])) return 1;
  }
  return 0;
}
int bl_parser_feed(bl_parser *p, uint8_t b) { return parser_step(p, b); }
int bl_parser_idle(bl_parser *p) {
  if (p->have == 0) return 0;
  uint8_t tmp[BL_MAX_PAYLOAD + 5]; size_t n = p->have - 1;
  memcpy(tmp, p->buf + 1, n); p->have = 0;
  for (size_t i = 0; i < n; i++) if (parser_step(p, tmp[i])) return 1;
  return 0;
}

int bl_popcount8(uint8_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
int bl_concurrent(int budget_ma, int pulse_ma) {
  if (pulse_ma <= 0) return 1;
  int c = budget_ma / pulse_ma; return c < 1 ? 1 : c;
}

int bl_plan(const uint8_t *cur, const uint8_t *tgt, int n, int conc, int mode,
            bl_pulse *out, int cap_pulses, uint16_t *slot_start, int cap_slots) {
  uint8_t diff[BL_MAX_CELLS]; uint8_t rem[BL_MAX_CELLS];
  int total = 0;
  if (n > BL_MAX_CELLS || conc < 1) return -1;
  for (int i = 0; i < n; i++) { diff[i] = cur[i] ^ tgt[i]; rem[i] = (uint8_t)bl_popcount8(diff[i]); total += rem[i]; }
  if (total > cap_pulses) return -1;
  int np = 0, ns = 0;
  while (total > 0) {
    if (ns + 1 >= cap_slots) return -1;
    slot_start[ns] = (uint16_t)np;
    uint8_t taken[BL_MAX_CELLS] = {0};
    for (int k = 0; k < conc; k++) {
      int best = -1;
      for (int i = 0; i < n; i++) {
        if (taken[i] || !rem[i]) continue;
        if (best < 0) best = i;
        else if (mode == BL_SCHED_FASTEST && rem[i] > rem[best]) best = i;
        if (mode == BL_SCHED_READING && best >= 0) break;
      }
      if (best < 0) break;
      taken[best] = 1;
      int d = 0; while (!((diff[best] >> d) & 1)) d++;
      diff[best] &= (uint8_t)~(1u << d); rem[best]--; total--;
      out[np].cell = (uint8_t)best; out[np].dot = (uint8_t)d; out[np].up = (tgt[best] >> d) & 1; np++;
    }
    ns++;
  }
  slot_start[ns] = (uint16_t)np;
  return ns;
}

void bl_dev_init(bl_dev *d, int ncells, uint16_t budget_ma, uint16_t pulse_ma, uint16_t pulse_us) {
  memset(d, 0, sizeof *d);
  d->ncells = ncells > BL_MAX_CELLS ? BL_MAX_CELLS : ncells;
  d->budget_ma = budget_ma; d->pulse_ma = pulse_ma; d->pulse_us = pulse_us; d->gap_us = 0;
  d->mode = BL_SCHED_FASTEST;
}

static size_t reply(uint8_t cmd, const uint8_t *pl, size_t n, uint8_t *out) { return bl_encode(cmd, pl, n, out); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

size_t bl_dev_handle(bl_dev *d, uint8_t cmd, const uint8_t *pl, size_t len, uint8_t *out) {
  uint8_t r[16];
  switch (cmd) {
  case BL_CMD_HELLO:
    r[0] = (uint8_t)d->ncells; r[1] = BL_DOTS;
    put16(r + 2, d->budget_ma); put16(r + 4, d->pulse_ma); put16(r + 6, d->pulse_us);
    return reply(BL_RSP_INFO, r, 8, out);
  case BL_CMD_SET_BUDGET:
    if (len != 2) { r[0] = BL_ERR_RANGE; return reply(BL_RSP_NAK, r, 1, out); }
    d->budget_ma = (uint16_t)(pl[0] << 8 | pl[1]);
    return reply(BL_RSP_ACK, (uint8_t[]){0,0,0,0,0,0,0,0}, 8, out);
  case BL_CMD_CLEAR:
  case BL_CMD_SET_LINE: {
    uint8_t tgt[BL_MAX_CELLS]; memcpy(tgt, d->cur, sizeof tgt);
    if (cmd == BL_CMD_CLEAR) memset(tgt, 0, sizeof tgt);
    else {
      if (len < 1 || (int)pl[0] + (int)(len - 1) > d->ncells) { r[0] = BL_ERR_RANGE; return reply(BL_RSP_NAK, r, 1, out); }
      memcpy(tgt + pl[0], pl + 1, len - 1);
    }
    bl_pulse pulses[BL_MAX_CELLS * BL_DOTS]; uint16_t starts[BL_MAX_CELLS * BL_DOTS + 1];
    int conc = bl_concurrent(d->budget_ma, d->pulse_ma);
    int ns = bl_plan(d->cur, tgt, d->ncells, conc, d->mode, pulses, BL_MAX_CELLS * BL_DOTS, starts, BL_MAX_CELLS * BL_DOTS);
    if (ns < 0) { r[0] = BL_ERR_RANGE; return reply(BL_RSP_NAK, r, 1, out); }
    for (int s = 0; s < ns && d->hal; s++) d->hal(d->hal_ctx, pulses + starts[s], starts[s + 1] - starts[s], d->pulse_us);
    memcpy(d->cur, tgt, sizeof tgt);
    uint32_t t = (uint32_t)ns * (d->pulse_us + d->gap_us);
    put16(r, (uint16_t)ns); put16(r + 2, starts[ns]);
    r[4] = (uint8_t)(t >> 24); r[5] = (uint8_t)(t >> 16); r[6] = (uint8_t)(t >> 8); r[7] = (uint8_t)t;
    d->total_pulses += starts[ns]; d->total_slots += (uint32_t)ns;
    return reply(BL_RSP_ACK, r, 8, out);
  }
  default:
    r[0] = BL_ERR_CMD; return reply(BL_RSP_NAK, r, 1, out);
  }
}

static const char TABLE[] = " A1B'K2L@CIF/MSP\"E3H9O6R^DJG>NTQ,*5<-U8V.%[$+X!&;:4\\0Z7(_?W]#Y)=";
uint8_t bl_ascii_dots(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 32);
  for (int i = 0; i < 64; i++) if (TABLE[i] == c) return (uint8_t)i;
  return 0;
}
