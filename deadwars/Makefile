# Dead Wars - GBA homebrew build
# Requires: arm-none-eabi-gcc (Debian/Ubuntu: gcc-arm-none-eabi libnewlib-arm-none-eabi), python3 + Pillow

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy

BUILD   := build
GEN     := $(BUILD)/gen
TARGET  := deadwars
# `make PERF=1` builds deadwars-perf.gba: cycle counters + a 60-zombie stress map
ifdef PERF
BUILD   := build-perf
TARGET  := deadwars-perf
endif
# optional: path to a GBA ROM you own, used only to copy the boot logo into the header
LOGO_ROM ?= ../baserom.gba

ARCH    := -mthumb -mthumb-interwork -mcpu=arm7tdmi -mtune=arm7tdmi
CFLAGS  := $(ARCH) -O2 -g -Wall -Wextra -Wno-unused-parameter -std=gnu11 \
           -ffunction-sections -fdata-sections -fno-strict-aliasing -I src -I $(GEN) $(if $(PERF),-DPERF)
LDFLAGS := $(ARCH) -nostartfiles -T gba.ld -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map -specs=nano.specs

SRCS    := $(wildcard src/*.c)
OBJS    := $(BUILD)/crt0.o $(GEN)/assets.o $(GEN)/music.o $(GEN)/lbart.o $(GEN)/story_data.o $(SRCS:src/%.c=$(BUILD)/%.o)
STORY   := $(wildcard data/story/*.tsv) tools/storyc.py
ART     := $(wildcard art/*.txt) tools/gfx.py

all: $(TARGET).gba

$(TARGET).gba: $(BUILD)/$(TARGET).elf tools/gbafix.py
	$(OBJCOPY) -O binary $< $@
	python3 -I tools/gbafix.py $@ --logo-from "$(LOGO_ROM)"

$(BUILD)/$(TARGET).elf: $(OBJS) gba.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lc -lgcc

$(GEN)/assets.c $(GEN)/assets.h: $(ART)
	python3 -I tools/gfx.py .

$(GEN)/music.c $(GEN)/music.h: audio/music.txt tools/music.py
	python3 -I tools/music.py .

# The Last Boat: generated art (tools/lbart.py) and storylets (tools/storyc.py, integrity-checked)
$(GEN)/lbart.c: art/lb.txt art/palettes.txt tools/lbart.py src/lbart.h
	python3 -I tools/lbart.py art/lb.txt $(GEN)

$(GEN)/story_data.c $(GEN)/story_data.h: $(STORY)
	python3 -I tools/storyc.py data/story $(GEN)

$(GEN)/lbart.o: $(GEN)/lbart.c
	$(CC) $(CFLAGS) -c $< -o $@

$(GEN)/story_data.o: $(GEN)/story_data.c
	$(CC) $(CFLAGS) -c $< -o $@

$(GEN)/music.o: $(GEN)/music.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/crt0.o: src/crt0.s
	@mkdir -p $(BUILD)
	$(CC) $(ARCH) -c $< -o $@

$(GEN)/assets.o: $(GEN)/assets.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c $(GEN)/assets.h $(GEN)/music.h $(GEN)/story_data.h $(wildcard src/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build build-perf deadwars.gba deadwars-perf.gba

.PHONY: all clean
