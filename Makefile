CC ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wpedantic -Werror
BUILD = build

all: $(BUILD)/test_core $(BUILD)/blbench $(BUILD)/blvec $(BUILD)/blcli

$(BUILD)/%: tests/%.c src/brailleline.c src/brailleline.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< src/brailleline.c
$(BUILD)/%: tools/%.c src/brailleline.c src/brailleline.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< src/brailleline.c

test: all
	$(BUILD)/test_core
	$(BUILD)/blvec > $(BUILD)/vectors.json
	node tests/test_js.mjs $(BUILD)/vectors.json

bench: $(BUILD)/blbench
	$(BUILD)/blbench tests/data/sample.txt

clean:
	rm -rf $(BUILD)
.PHONY: all test bench clean
