# snespico8 SNES-side build. M0 spikes for now.
# Calypsi C for the runtime core (see docs/PROJECT.md), WLA-DX for probe
# ROMs and later asm glue.

CALDIR := ../tools/calypsi-65816-5.18
CC := $(CALDIR)/bin/cc65816
AS := $(CALDIR)/bin/as65816
LN := $(CALDIR)/bin/ln65816

PVBIN := ../../pvsneslib/devkitsnes/bin
WLA := $(PVBIN)/wla-65816
WLALINK := $(PVBIN)/wlalink

MESEN := /Applications/Mesen.app/Contents/MacOS/Mesen

MODEL := --code-model=large --data-model=large
OPT ?= -O2
CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul $(OPT) --list-file=$(@:.o=.lst)
AFLAGS := --core=65816 --target=SNES $(MODEL)
LNFLAGS := calypsi/linker.scm --output-format=raw --rtattr exit=simplified \
           $(CALDIR)/lib/clib-lc-ld-snes-noppu.a

BUILD := build

.PHONY: all hello test-hello m7demo test-m7demo clean
all: hello m7demo
hello: $(BUILD)/hello.sfc
m7demo: $(BUILD)/m7demo.sfc

$(BUILD)/m7tables.inc: tools/mkm7.py | $(BUILD)
	python3 tools/mkm7.py $@

$(BUILD)/m7demo.o: m7demo.asm $(BUILD)/m7tables.inc | $(BUILD)
	$(WLA) -o $@ $<

$(BUILD)/m7demo.sfc: $(BUILD)/m7demo.o
	printf '[objects]\n$(BUILD)/m7demo.o\n' > $(BUILD)/link_m7
	$(WLALINK) -d -s -c $(BUILD)/link_m7 $@

test-m7demo: $(BUILD)/m7demo.sfc
	sh test/run_m7.sh

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/header.o: calypsi/header.s | $(BUILD)
	$(AS) -o $@ $< $(AFLAGS)

$(BUILD)/mailbox.o: calypsi/mailbox.c calypsi/mailbox.h | $(BUILD)
	$(CC) -o $@ $< $(CFLAGS)

$(BUILD)/hello_main.o: hello.c calypsi/mailbox.h | $(BUILD)
	$(CC) -o $@ $< $(CFLAGS)

$(BUILD)/hello.raw: $(BUILD)/header.o $(BUILD)/mailbox.o $(BUILD)/hello_main.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/hello.map $^

$(BUILD)/hello.sfc: $(BUILD)/hello.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-hello: $(BUILD)/hello.sfc
	$(MESEN) --testrunner $(BUILD)/hello.sfc test/hello.lua

clean:
	rm -rf $(BUILD)
