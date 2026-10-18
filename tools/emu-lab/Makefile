# Emu lab: headless libretro runner + GBA cores built from source. See docs/miyoo/EMU_LAB.md
CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -std=c11
CORES   := cores
# gpSP revision shipped in Onion 4.4 (strings in Onion's gpsp_libretro.so: "v1.1.0-6250979")
GPSP_ONION_REV := 6250979
SYSTEM_MGBA := /usr/lib/x86_64-linux-gnu/libretro/mgba_libretro.so

all: lrun cores

lrun: lrun.c libretro.h
	$(CC) $(CFLAGS) -o $@ lrun.c -ldl

cores: $(CORES)/gpsp_libretro.so $(CORES)/gpsp-onion_libretro.so $(CORES)/mgba_libretro.so

$(CORES)/src/gpsp:
	@mkdir -p $(CORES)/src
	git clone -q https://github.com/libretro/gpsp.git $@

$(CORES)/gpsp_libretro.so: | $(CORES)/src/gpsp
	$(MAKE) -C $(CORES)/src/gpsp -j8
	cp $(CORES)/src/gpsp/gpsp_libretro.so $@
	git -C $(CORES)/src/gpsp log -1 --format='gpsp master: %H %cd' > $(CORES)/gpsp.version

$(CORES)/gpsp-onion_libretro.so: | $(CORES)/src/gpsp
	rm -rf $(CORES)/src/gpsp-onion
	git -C $(CORES)/src/gpsp worktree prune
	git -C $(CORES)/src/gpsp worktree add -f ../gpsp-onion $(GPSP_ONION_REV)
	$(MAKE) -C $(CORES)/src/gpsp-onion -j8
	cp $(CORES)/src/gpsp-onion/gpsp_libretro.so $@

# Debian/Ubuntu libretro-mgba (0.10.2); Onion ships "0.10-dev ec5ecb26d"
$(CORES)/mgba_libretro.so:
	@mkdir -p $(CORES)
	ln -sf $(SYSTEM_MGBA) $@

clean:
	rm -f lrun

.PHONY: all cores clean
