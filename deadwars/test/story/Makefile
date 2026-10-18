# Dead Wars - storylet compiler selftest + host fuzz of the runtime (src/story.c)
#   make run                  storyc --selftest, then fuzz src/story.c with data/story and edge/
#   make run RUNS=5000 SEED=7 more runs / another seed
#   make run SAN=             without the address/undefined-behaviour sanitizers
#   make run OUT=/tmp/st      build somewhere else
# Fixtures: min/ is the smallest valid data; each bad_*/ changes it (X.tsv replaces a table,
# X.tsv.append adds rows, X.tsv.delete drops it) and `expect` holds the message it must produce.
# edge/ is valid data with every feature at its limits; it is fuzzed with -d.
ROOT    := ../..
SRC     := $(ROOT)/src
GEN     := $(ROOT)/build/gen
OUT     ?= $(ROOT)/build/story-test
HOSTCC  ?= gcc
SAN     ?= -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -ftrivial-auto-var-init=pattern
CFLAGS  := -O1 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -fno-strict-aliasing -DHOST_SIM $(SAN)
RUNS    ?= 2000
SEED    ?= 1
STORYC  := $(ROOT)/tools/storyc.py $(SRC)/lb.h $(SRC)/story.h $(SRC)/lb_data.c $(SRC)/text.c
GAME    := $(SRC)/story.c $(SRC)/person.c $(SRC)/lb_data.c
DEPS    := story_fuzz.c $(GAME) $(wildcard $(SRC)/*.h) $(GEN)/assets.h

all: $(OUT)/story_fuzz $(OUT)/story_fuzz_edge

$(OUT)/data/story_data.c: $(wildcard $(ROOT)/data/story/*.tsv) $(STORYC)
	python3 -I $(ROOT)/tools/storyc.py $(ROOT)/data/story $(OUT)/data

$(OUT)/edge/story_data.c: $(wildcard edge/*.tsv) $(STORYC)
	python3 -I $(ROOT)/tools/storyc.py edge $(OUT)/edge

$(OUT)/story_fuzz: $(OUT)/data/story_data.c $(DEPS)
	$(HOSTCC) $(CFLAGS) -I $(SRC) -I $(OUT)/data -I $(GEN) -o $@ story_fuzz.c $< $(GAME)

$(OUT)/story_fuzz_edge: $(OUT)/edge/story_data.c $(DEPS)
	$(HOSTCC) $(CFLAGS) -I $(SRC) -I $(OUT)/edge -I $(GEN) -o $@ story_fuzz.c $< $(GAME)

selftest:
	python3 -I $(ROOT)/tools/storyc.py --selftest

run: selftest all
	$(OUT)/story_fuzz -n $(RUNS) -s $(SEED)
	$(OUT)/story_fuzz_edge -n $(RUNS) -s $(SEED) -d

.PHONY: all selftest run
