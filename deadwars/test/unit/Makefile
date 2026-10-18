# Dead Wars - host unit tests for the tactical rules (src/tac_rules.c)
#   make            build ../../build/unit/tac_test
#   make run        build and run every test; exits nonzero on any failure
#   make run SAN=   build without the address/undefined-behaviour sanitizers
#   make run LBDATA=/path/to/lb_data.c   check another copy of the slice data
ROOT    := ../..
SRC     := $(ROOT)/src
GEN     := $(ROOT)/build/gen
OUT     ?= $(ROOT)/build/unit
HOSTCC  ?= gcc
LBDATA  ?= $(SRC)/lb_data.c
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing \
           -DHOST_SIM -I $(SRC) -I $(GEN) -I . $(SAN)
GAME    := $(SRC)/map.c $(SRC)/data.c $(SRC)/missions.c $(LBDATA) $(SRC)/person.c $(SRC)/tac_rules.c
TESTS   := test_tac.c stubs.c

all: $(OUT)/tac_test

$(GEN)/assets.h:
	$(MAKE) -C $(ROOT)

$(OUT)/tac_test: $(TESTS) unit.h $(GAME) $(wildcard $(SRC)/*.h) $(GEN)/assets.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $(TESTS) $(GAME)

run: $(OUT)/tac_test
	$(OUT)/tac_test

.PHONY: all run
