# Dead Wars - host unit tests for the tactical rules (src/tac_rules.c), the horde turn (tac_ai.c),
# the mission flow (tac.c), the run layer (run.c, runsave.c) and the battery save (save.c)
#   make            build ../../build/unit/tac_test, flow_test, run_test and save_test
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
FLOW    := $(SRC)/tac_ai.c $(SRC)/tac_gfx.c $(GEN)/lbart.c

all: $(OUT)/tac_test $(OUT)/flow_test $(OUT)/run_test $(OUT)/save_test

$(GEN)/assets.h $(GEN)/lbart.c:
	$(MAKE) -C $(ROOT)

$(OUT)/tac_test: $(TESTS) unit.h $(GAME) $(wildcard $(SRC)/*.h) $(GEN)/assets.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $(TESTS) $(GAME)

# test_flow.c includes tac.c (and its tac_fx_* hooks) for the static helpers
$(OUT)/flow_test: test_flow.c stubs.c unit.h $(GAME) $(FLOW) $(SRC)/tac.c $(wildcard $(SRC)/*.h) $(GEN)/assets.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -DTAC_FLOW_TEST -o $@ test_flow.c stubs.c $(GAME) $(FLOW)

# test_run.c includes run.c for its static helpers; its screens and storylets are stubbed there
$(OUT)/run_test: test_run.c stubs.c unit.h $(GAME) $(SRC)/run.c $(SRC)/runsave.c $(wildcard $(SRC)/*.h) $(GEN)/assets.h $(GEN)/lbart.c
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -DRUN_TEST -o $@ test_run.c stubs.c $(GAME) $(SRC)/runsave.c $(GEN)/lbart.c

# test_save.c includes save.c; SRAM is host_sram
$(OUT)/save_test: test_save.c unit.h $(SRC)/save.c $(wildcard $(SRC)/*.h) $(GEN)/assets.h
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ test_save.c

run: $(OUT)/tac_test $(OUT)/flow_test $(OUT)/run_test $(OUT)/save_test
	$(OUT)/tac_test
	$(OUT)/flow_test
	$(OUT)/run_test
	$(OUT)/save_test

.PHONY: all run
