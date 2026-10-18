# Dead Wars - host-side balance simulator (see README.md)
#   make            build ../../build/sim/sim
#   make run        40 seeds of every mission
ROOT    := ../..
SRC     := $(ROOT)/src
GEN     := $(ROOT)/build/gen
OUT     := $(ROOT)/build/sim
HOSTCC  ?= gcc
CFLAGS  := -O2 -g -std=gnu11 -Wall -Wno-unused-parameter -Wno-unused-function -fno-strict-aliasing \
           -DHOST_SIM -I $(SRC) -I $(GEN)
GAME    := $(SRC)/map.c $(SRC)/battle.c $(SRC)/ai.c $(SRC)/data.c $(SRC)/missions.c $(SRC)/ui.c
SEEDS   ?= 40

all: $(OUT)/sim

$(GEN)/assets.h:
	$(MAKE) -C $(ROOT)

$(OUT)/sim: sim.c stubs.c $(GAME) $(wildcard $(SRC)/*.h) $(GEN)/assets.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ sim.c stubs.c $(GAME)

run: $(OUT)/sim
	$(OUT)/sim -n $(SEEDS)

.PHONY: all run
