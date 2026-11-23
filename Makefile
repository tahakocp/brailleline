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

sanitize:
	@mkdir -p $(BUILD)
	$(CC) -std=c99 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -o $(BUILD)/test_asan tests/test_core.c src/brailleline.c
	$(BUILD)/test_asan
.PHONY: sanitize
