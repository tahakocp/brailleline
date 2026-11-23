# Serial protocol

One frame per message, both directions. Bytes on the wire:

```
0xA5 | cmd | len | payload (len bytes, len <= 96) | crc_hi | crc_lo
```

The CRC is CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection) over `cmd`, `len` and the payload. `bl_crc16("123456789")` is 0x29B1, which the tests check.

| cmd | direction | payload | meaning |
|---|---|---|---|
| 0x01 HELLO | host to device | none | ask what the device is |
| 0x81 INFO | device to host | cells u8, dots u8, budget_mA u16, pulse_mA u16, pulse_us u16 | answer to HELLO |
| 0x02 SET_LINE | host to device | offset u8, then one byte per cell | bit 0 is dot 1, bit 7 is dot 8, the same layout as Unicode braille patterns (U+2800 + value) |
| 0x03 SET_BUDGET | host to device | budget_mA u16 | change the supply budget |
| 0x04 CLEAR | host to device | none | lower every dot |
| 0x82 ACK | device to host | slots u16, pulses u16, time_us u32 | the line was accepted and planned (SET_BUDGET answers zeros) |
| 0x83 NAK | device to host | code u8 | 1 crc, 2 range, 3 unknown command |

Integers are big endian.

## Resync

A byte that is not 0xA5 is ignored while idle. A frame with a bad length or a bad CRC is dropped and its bytes after the first one are scanned again, because the 0xA5 may have been noise. A header made of noise can still announce a long frame and swallow the real one that follows, so a UART driver should call `bl_parser_idle()` after a few byte times of silence. The fuzz test sends 3000 frames through random junk with an idle call before each frame and gets all 3000 back.

## What the device does with SET_LINE

1. Compare the new cells with the dots that are up now. Only the differences cost anything.
2. Work out how many coils may fire together: `budget_mA / pulse_mA`, at least 1.
3. Build a plan: a list of slots, each slot a set of (cell, dot, up/down) pulses. One cell fires at most one coil per slot because the driver row is shared.
4. Hand each slot to the HAL callback (`hal(ctx, pulses, count, pulse_us)`), then record the new state.

The `fastest` mode takes the cells with the largest backlog first. That reaches `max(largest per-cell backlog, ceil(dots / concurrent))` slots, which is the lower bound for these rules, and the tests check it on 20,000 random lines. The `reading` mode starts with the leftmost cells so text settles in reading order, and costs a few percent more slots (see the numbers in the README).

## Not covered

No key or routing-cell events yet, no 8-dot contraction handling, no flow control beyond the ACK. A BRLTTY driver for this protocol is not written.
