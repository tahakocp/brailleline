/* brailleline: controller core for a refreshable braille line.
 * Plain C99, no malloc, no libc beyond string.h. MIT licensed. */
#ifndef BRAILLELINE_H
#define BRAILLELINE_H
#include <stdint.h>
#include <stddef.h>

#define BL_MAX_CELLS 80
#define BL_DOTS 8
#define BL_MAX_PAYLOAD 96
#define BL_SOF 0xA5

/* host -> device */
#define BL_CMD_HELLO      0x01
#define BL_CMD_SET_LINE   0x02  /* offset, cells... */
#define BL_CMD_SET_BUDGET 0x03  /* u16 milliamps */
#define BL_CMD_CLEAR      0x04
/* device -> host */
#define BL_RSP_INFO       0x81  /* cells, dots, budget_mA u16, pulse_mA u16, pulse_us u16 */
#define BL_RSP_ACK        0x82  /* slots u16, pulses u16, time_us u32 */
#define BL_RSP_NAK        0x83  /* code */

#define BL_ERR_CRC 1
#define BL_ERR_RANGE 2
#define BL_ERR_CMD 3

/* scheduling modes */
#define BL_SCHED_FASTEST 0  /* fewest slots (largest backlog first) */
#define BL_SCHED_READING 1  /* leftmost cells settle first */

typedef struct { uint8_t cell; uint8_t dot; uint8_t up; } bl_pulse;

uint16_t bl_crc16(const uint8_t *p, size_t n);
size_t bl_encode(uint8_t cmd, const uint8_t *payload, size_t len, uint8_t *out);

typedef struct {
  uint8_t buf[BL_MAX_PAYLOAD + 5];
  size_t have;
  uint32_t crc_errors, frames;
  uint8_t cmd, len, payload[BL_MAX_PAYLOAD];
} bl_parser;
void bl_parser_init(bl_parser *p);
/* returns 1 when a valid frame is complete (fields cmd/len/payload set) */
int bl_parser_feed(bl_parser *p, uint8_t byte);
/* call after a silent gap (a few byte times): drops a half-received frame and
 * re-scans its bytes, so a noise header cannot swallow the next real frame. Returns 1 if a frame came out. */
int bl_parser_idle(bl_parser *p);

/* Plan: which dots to pulse in which slot. cap_* are output capacities.
 * slot_start has nslots+1 entries. Returns nslots, or -1 if capacity is too small. */
int bl_plan(const uint8_t *cur, const uint8_t *tgt, int ncells, int concurrent,
            int mode, bl_pulse *out, int cap_pulses, uint16_t *slot_start, int cap_slots);
int bl_concurrent(int budget_ma, int pulse_ma);
int bl_popcount8(uint8_t v);

typedef void (*bl_hal_pulse_fn)(void *ctx, const bl_pulse *p, int count, uint32_t us);
typedef struct {
  int ncells;
  uint16_t budget_ma, pulse_ma, pulse_us, gap_us;
  int mode;
  uint8_t cur[BL_MAX_CELLS];
  bl_hal_pulse_fn hal; void *hal_ctx;
  uint32_t total_pulses, total_slots;
} bl_dev;
void bl_dev_init(bl_dev *d, int ncells, uint16_t budget_ma, uint16_t pulse_ma, uint16_t pulse_us);
/* handle one frame, write reply frame to out (>= BL_MAX_PAYLOAD+5), return reply length */
size_t bl_dev_handle(bl_dev *d, uint8_t cmd, const uint8_t *pl, size_t len, uint8_t *out);

/* North American braille ASCII -> 6-dot pattern (bit0 = dot 1 ... bit5 = dot 6). 0 for unknown. */
uint8_t bl_ascii_dots(char c);
#endif
