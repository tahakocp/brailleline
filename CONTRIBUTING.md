# Contributing

`make test` builds the C core with `-Wall -Wextra -Wpedantic -Werror`, runs the tests, writes test vectors from the C code and checks the JavaScript port against them. Node 18 or newer. `make sanitize` runs the C tests under ASan and UBSan.

If you change the scheduler or the frame format, change both `src/brailleline.c` and `web/blcore.js`; the vector test fails if they drift apart.

The most useful contribution is a measurement from real hardware: your pulse current, pulse time and how many coils your supply really carries at once, plus whether the plan order caused trouble (heating of one cell, a pin that failed to latch). Open an issue with the numbers.

Other things that would help:
- a HAL for RP2040 or STM32 that drives shift registers and half-bridges from the slot callback
- a BRLTTY driver that speaks the protocol in `docs/protocol.md`
- key and routing events
- Grade 2 translation through liblouis on the host side
