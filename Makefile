CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
CFLAGS  += -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L
BUILD   := build

ENGINE  := src/knightshift.c src/bot.c
HEADERS := src/knightshift.h src/bot.h

.PHONY: all play web test clean

all: $(BUILD)/knightshift $(BUILD)/knightshift-web

$(BUILD):
	mkdir -p $(BUILD)

# terminal game
$(BUILD)/knightshift: $(ENGINE) src/cli.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) src/cli.c

# local web server for the browser UI
$(BUILD)/knightshift-web: $(ENGINE) src/server.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(ENGINE) src/server.c

$(BUILD)/tests: $(ENGINE) tests/test_knightshift.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -Isrc -o $@ $(ENGINE) tests/test_knightshift.c

play: $(BUILD)/knightshift
	./$(BUILD)/knightshift

web: $(BUILD)/knightshift-web
	./$(BUILD)/knightshift-web

test: $(BUILD)/tests
	./$(BUILD)/tests

clean:
	rm -rf $(BUILD)
