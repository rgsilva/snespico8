# snespico8 SNES-side build. M0 spikes for now.
# Calypsi C for the runtime core (see docs/PROJECT.md), WLA-DX for probe
# ROMs and later asm glue.

CALDIR := ../tools/calypsi-65816-5.18
CC := $(CALDIR)/bin/cc65816
AS := $(CALDIR)/bin/as65816
LN := $(CALDIR)/bin/ln65816

# The full runtime (M3+) builds with 5.17 at -O0 — the correctness
# baseline established in M0b (the -O2 sequencing miscompiles are avoided
# at -O0; see bugs/). The M0 spikes above still use 5.18/-O2.
BUILD := build
P8CALDIR := ../tools/calypsi-65816-5.17
P8CC := $(P8CALDIR)/bin/cc65816
P8AS := $(P8CALDIR)/bin/as65816
P8LN := $(P8CALDIR)/bin/ln65816
CORE := ../core-c/src
# Recursive (=): --list-file expands per-target so every object gets a .lst
# (the M9 codegen fences scan them). P8_VMOPS_ADDR pins the lvm.c op counter
# at its fixed MB_VMOPS mailbox slot ($7E:F00C) so the bench harness samples
# it live and link-layout changes can never move it.
P8OPT ?= -O0
# M9 T7: --no-cross-call/--no-interprocedural-cross-jump — Calypsi factors
# common 3-5 instruction sequences into jsl/rtl helpers even at -O2 --speed
# (1,187 call sites in lua_lvm.lst alone, 14 cycles of pure call overhead
# each, ~5 of them on the VM dispatch path per op). Disabling trades ROM
# size for cycles everywhere; measured on bench_tab (docs/M9-perf-plan.md).
P8CFLAGS = --core=65816 --target=snes --code-model=large --data-model=large \
           --no-ppu-mul --no-cross-call --no-interprocedural-cross-jump \
           $(P8OPT) -I $(CORE) -I . \
           -DP8_VMOPS_ADDR=0x7ef00cul -DP8_HWMATH --list-file=$(@:.o=.lst)
LUAOBJS := lapi lauxlib lbaselib lcode lctype lcorolib ldebug ldo ldump \
           lfunc lgc llex lmem lobject lopcodes lparser lstate lstring \
           ltable ltm lundump lvm lzio lpico8lib
COREOBJS := p8num p8 p8gfx p8audio
P8LUA_O := $(addprefix $(BUILD)/p8obj/lua_,$(addsuffix .o,$(LUAOBJS)))
P8CORE_O := $(addprefix $(BUILD)/p8obj/,$(addsuffix .o,$(COREOBJS)))

PVBIN := ../../pvsneslib/devkitsnes/bin
WLA := $(PVBIN)/wla-65816
WLASPC := $(PVBIN)/wla-spc700
WLALINK := $(PVBIN)/wlalink

MESEN := /Applications/Mesen.app/Contents/MacOS/Mesen
# Mesen's testrunner kills scripts at a default wall limit; long runs
# (celeste: ~1 min emulated parse, slower under host load) pass an
# explicit --timeout in wall seconds. Short self-capping harnesses
# (p8bench/p8prof stop themselves) don't need it.
MESENLONG := $(MESEN) --testrunner --timeout=1800

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

# ---- M3: the runtime core booting an embedded cart ------------------
.PHONY: p8 test-p8
p8: $(BUILD)/p8.sfc

CART ?= ../core-c/tests/carts/m3_boot.p8

# cart_hello.h always regenerates (FORCE), but cart2c.py is write-if-changed,
# so its mtime only bumps when CART actually changes — switching carts
# rebuilds p8main.o without relying on (1-second-granular) mtimes.
# FRAMES = frames the main loop runs (M4 parity keeps it small so the
# extra per-flip hashing fits Mesen's script time limit). INPUT = optional
# sidecar file of per-frame hex button masks.
FRAMES ?= 8
# PLAYCART = the cart the interactive `play` target builds (see below).
PLAYCART ?= ../core-c/tests/carts/m7_input.p8
# MENUCARTS = the carts the multi-cart menu/test-menu targets bundle.
MENUCARTS ?= ../core-c/tests/carts/m7_input.p8 ../core-c/tests/carts/m6_demo.p8 ../core-c/tests/carts/m6_fx.p8 ../core-c/tests/carts/m3_boot.p8
.PHONY: FORCE
$(BUILD)/cart_hello.h: $(CART) tools/cart2c.py tools/p8png.py FORCE | $(BUILD)
	python3 tools/cart2c.py $(CART) $@ $(FRAMES) $(INPUT)

# ---- M9 T2: per-TU optimization levels ------------------------------
# The hot TUs (the plan's list: VM, tables, GC, strings, calls, fix32,
# gfx, API) build -O2 --speed (Calypsi defaults to --space even at -O2).
# llex/lparser and everything else stay -O0: both known -O2 miscompiles
# live in the lexer (bugs/), and parse is cart-load-only. Gated by the
# roulette battery (core-c/tests/roulette.sh) because Calypsi allocator
# failures are link-layout-sensitive, not deterministic.
P8OPT_HOT ?= -O2 --speed
# P8HOT / P8HOT1 are overridable for miscompile bisection: build with
# e.g. P8HOT="lua_lvm p8num" P8HOT1= to optimize only those TUs
# (rm build/p8obj between configs -- opt flags are not a make dep).
# lua_ltable is NOT in the -O2 set: at -O2 it miscompiles an allocation
# size during lua_newstate ("memory allocation error: block too big"
# panic at boot -- bisected 2026-07-21, all other hot TUs pass).
P8HOT ?= lua_lvm lua_lgc lua_lstring lua_ldo p8num p8 p8gfx
P8HOT1 ?=
$(addprefix $(BUILD)/p8obj/,$(addsuffix .o,$(P8HOT))): P8OPT = $(P8OPT_HOT)
$(addprefix $(BUILD)/p8obj/,$(addsuffix .o,$(P8HOT1))): P8OPT = -O1

# M9 T2 roulette: PERTURB=n links a pad object early in the link order
# (n dummy functions + a byte array), shifting every downstream address.
# One green parity run proves nothing when the failure mode is per-layout
# roulette; parity must hold across several perturbed layouts.
PERTURB ?= 0
$(BUILD)/p8obj/pad.c: tools/mkpad.py FORCE | $(BUILD)/p8obj
	python3 tools/mkpad.py $@ $(PERTURB)

$(BUILD)/p8obj/pad.o: $(BUILD)/p8obj/pad.c
	$(P8CC) -o $@ $< $(P8CFLAGS)

$(BUILD)/p8obj:
	mkdir -p $(BUILD)/p8obj

$(BUILD)/p8obj/lua_%.o: $(CORE)/lua/%.c | $(BUILD)/p8obj
	$(P8CC) -o $@ $< $(P8CFLAGS)

$(BUILD)/p8obj/%.o: $(CORE)/%.c | $(BUILD)/p8obj
	$(P8CC) -o $@ $< $(P8CFLAGS)

# P8MAINDEF: parity builds pass -DP8_PARITY to enable per-flip RAM hashes.
# The default (demo/boot) ROM omits them so it fits the script time limit.
P8MAINDEF ?=
$(BUILD)/p8obj/p8main.o: p8main.c p8spc.h $(BUILD)/cart_hello.h | $(BUILD)/p8obj
	$(P8CC) -o $@ p8main.c $(P8CFLAGS) -I $(BUILD) $(P8MAINDEF)

$(BUILD)/p8obj/p8heap.o: p8heap.c p8heap.h | $(BUILD)/p8obj
	$(P8CC) -o $@ p8heap.c $(P8CFLAGS)

# M9 T5: CPU-multiplier/divider helpers (P8_HWMATH routes p8num here).
# MUST build -O0: Calypsi -O2 DELETES volatile accesses made through
# cast-constant pointers (observed: the whole divide chain compiled to
# two reads with no trigger stores -- 5th documented miscompile; the
# A/B ROM caught it as stale-divider garbage). All hardware-register
# code in this project is -O0 for this reason.
$(BUILD)/p8obj/hwmath.o: hwmath.c | $(BUILD)/p8obj
	$(P8CC) -o $@ hwmath.c $(P8CFLAGS)

# ---- SPC700 audio driver (M6) --------------------------------------
# The driver is assembled with WLA-SPC700 into a 64KB RAM image; spc2c.py
# extracts the .ORG $0200 region into a C blob (p8spc_blob.h) that the
# Calypsi build embeds and apu_upload (p8spc.c) streams into the APU at
# boot. Waveform BRR + pitch table come from tools/mkmiditab.py.
$(BUILD)/p8spc_tables.asm: tools/mkp8snd.py | $(BUILD)
	python3 tools/mkp8snd.py $@

$(BUILD)/p8spc.o: p8spc.asm $(BUILD)/p8spc_tables.asm | $(BUILD)
	$(WLASPC) -o $@ p8spc.asm

$(BUILD)/p8spc64.bin: $(BUILD)/p8spc.o linkfile_p8spc
	$(WLALINK) -d -s linkfile_p8spc $@

$(BUILD)/p8spc_blob.h: $(BUILD)/p8spc64.bin tools/spc2c.py
	python3 tools/spc2c.py $< $@

$(BUILD)/p8obj/p8spc.o: p8spc.c p8spc.h $(BUILD)/p8spc_blob.h | $(BUILD)/p8obj
	$(P8CC) -o $@ p8spc.c $(P8CFLAGS) -I $(BUILD)

$(BUILD)/p8obj/stubs.o: stubs.c calypsi/mailbox.h | $(BUILD)/p8obj
	$(P8CC) -o $@ stubs.c $(P8CFLAGS)

$(BUILD)/p8obj/mailbox.o: calypsi/mailbox.c calypsi/mailbox.h | $(BUILD)/p8obj
	$(P8CC) -o $@ calypsi/mailbox.c $(P8CFLAGS)

$(BUILD)/p8obj/header.o: calypsi/header.s | $(BUILD)/p8obj
	$(P8AS) -o $@ $< --core=65816 --target=SNES --code-model=large --data-model=large

$(BUILD)/p8.raw: $(BUILD)/p8obj/pad.o $(P8LUA_O) $(P8CORE_O) $(BUILD)/p8obj/p8main.o \
                 $(BUILD)/p8obj/p8heap.o $(BUILD)/p8obj/hwmath.o $(BUILD)/p8obj/p8spc.o \
                 $(BUILD)/p8obj/stubs.o \
                 $(BUILD)/p8obj/mailbox.o $(BUILD)/p8obj/header.o
	python3 tools/lstcheck.py $(BUILD)/p8obj
	$(P8LN) -o $@ calypsi/linker.scm --output-format=raw --rtattr exit=simplified \
	  $(P8CALDIR)/lib/clib-lc-ld-snes-noppu.a --list-file=$(BUILD)/p8.map $^

$(BUILD)/p8.sfc: $(BUILD)/p8.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-p8: $(BUILD)/p8.sfc
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/p8boot.lua

# M8b: .p8.png loader. Builds a ROM straight from a real PICO-8 PNG export
# (cart2c decodes stego + pxa at build time -- nothing on the SNES) and
# checks demo_video's palette bars render, proving the whole PNG->ROM path.
.PHONY: test-png
test-png:
	$(MAKE) p8 CART=../core-c/tests/carts/demo_video.p8.png FRAMES=8
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/png.lua

# M8: Start-triggered pause menu. m8_pause.p8 registers menuitem() callbacks;
# test/pause.lua drives Start -> navigate -> select and checks the callback
# ran. FRAMES huge so the cart runs interactively.
.PHONY: test-pause
test-pause:
	$(MAKE) p8 CART=../core-c/tests/carts/m8_pause.p8 FRAMES=1000000000
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/pause.lua

# M6a: APU bring-up. Builds the ROM with the SPC bring-up path enabled
# (uploads the driver, keys 3 voices, holds) and checks in Mesen that the
# driver's voice mask at SPC $00 went non-zero — proof the whole
# upload + $2140-$2143 transport + DSP path works. Force-rebuild p8main.o
# so the -DP8_SPC_TEST define takes (P8MAINDEF isn't a file dep).
.PHONY: test-spc
test-spc:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 P8MAINDEF=-DP8_SPC_TEST
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/spc.lua
	rm -f $(BUILD)/p8obj/p8main.o

# M6c: the autonomous SPC sequencer. Builds with -DP8_SPC_SEQ and a cart
# that has sfx (m6_audio), streams the audio into the SPC and plays sfx 0;
# test/spcseq.lua checks the timer heartbeat, the streamed data, and the
# channel's row advancing on its own -- i.e. real-time sequencing.
.PHONY: test-spcseq
test-spcseq:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 P8MAINDEF=-DP8_SPC_SEQ CART=../core-c/tests/carts/m6_audio.p8
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/spcseq.lua
	rm -f $(BUILD)/p8obj/p8main.o

# M6d: end-to-end. A NORMAL ROM (no test define) runs a cart whose _init
# calls music(0); the core routes it to the SPC, which plays the song in
# real time while the game loop runs. test/audio.lua checks the SPC is
# actually playing + chaining patterns -- driven only by the cart's Lua.
.PHONY: test-audio
test-audio:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 CART=../core-c/tests/carts/m6_audio.p8 FRAMES=16
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/audio.lua
	rm -f $(BUILD)/p8obj/p8main.o

# M6 effects: sfx0 is a DROP note; test/fx.lua checks the SPC actually
# modulates the voice pitch per tick (a wide pitch sweep).
.PHONY: test-fx
test-fx:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 CART=../core-c/tests/carts/m6_fx.p8 FRAMES=8
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/fx.lua
	rm -f $(BUILD)/p8obj/p8main.o

# M7a: live controller input. A NORMAL ROM (no test define, no baked input
# trace) runs m7_input.p8; test/input.lua drives the SNES pad with setInput
# and checks the dot moved via btn() -- proving the auto-joypad read reaches
# the cart. FRAMES is huge so the loop stays alive for the whole run.
.PHONY: test-input
test-input:
	$(MAKE) p8 CART=../core-c/tests/carts/m7_input.p8 FRAMES=100000
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/input.lua

# A loadable audio-demo ROM: a real melody + bass that loops (the SPC plays
# it in real time) with a slowly-updating screen. Build then open
# build/p8.sfc in Mesen with sound ON -- this is meant to be heard, not run
# headless. FRAMES is huge so the video keeps creeping for a long time; the
# music loops forever regardless.
.PHONY: demo
demo:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 CART=../core-c/tests/carts/m6_demo.p8 FRAMES=60000
	rm -f $(BUILD)/p8obj/p8main.o
	@echo "==> Loadable demo at $(BUILD)/p8.sfc -- open in Mesen with sound on."

# M7: build any cart as an INTERACTIVE ROM (huge FRAMES so it runs until
# reset) that reads the live SNES pad. Defaults to the m7_input dot probe;
# override with e.g. `make play PLAYCART=path/to/cart.p8`. Open build/p8.sfc
# in Mesen (or on hardware) and drive it with a controller.
.PHONY: play
play:
	$(MAKE) p8 CART="$(PLAYCART)" FRAMES=1000000000
	@echo "==> Interactive ROM at $(BUILD)/p8.sfc -- open in Mesen and drive it with a pad."

# M8: bundle several carts into build/cart_multi.h and build the menu ROM
# (-DP8_MULTICART). p8main.c then boots a d-pad picker instead of one cart;
# each pick runs in a fresh lua_State, Select returns to the list.
$(BUILD)/cart_multi.h: tools/multicart2c.py tools/cart2c.py tools/p8png.py FORCE | $(BUILD)
	python3 tools/multicart2c.py $@ $(MENUCARTS)

.PHONY: menu test-menu
menu: $(BUILD)/cart_multi.h
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 P8MAINDEF=-DP8_MULTICART
	rm -f $(BUILD)/p8obj/p8main.o
	@echo "==> Menu ROM at $(BUILD)/p8.sfc -- open in Mesen, pick a cart with the pad (O launches, Select returns)."

test-menu: $(BUILD)/cart_multi.h
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 P8MAINDEF=-DP8_MULTICART
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/menu.lua
	rm -f $(BUILD)/p8obj/p8main.o

# M13: the demo ROM -- the multi-cart menu curated with our own homemade
# test carts (all ours, so freely shippable): a graphics test, a live
# joypad readout, and the music demo. Open build/p8.sfc in Mesen with sound
# on, pick with the pad. (Just `menu` with a curated MENUCARTS.)
DEMOCARTS = ../core-c/tests/carts/demo_video.p8 ../core-c/tests/carts/demo_input.p8 ../core-c/tests/carts/m6_demo.p8
.PHONY: demo-rom
demo-rom:
	$(MAKE) menu MENUCARTS="$(DEMOCARTS)"
	@echo "==> Demo ROM at $(BUILD)/p8.sfc -- video / joypad / audio, pick with the pad."

# Verify the demo ROM: the menu lists our carts and cart 0 (demo_video)
# launches + renders. test/demorom.lua also prints SHOT <name> <hex> lines
# (Mesen screenshots) you can pipe through xxd -r -p to eyeball.
.PHONY: test-demo
test-demo:
	$(MAKE) demo-rom
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/demorom.lua

# M14: the free public release ROM -- the demo ROM (our runtime + our own
# demo carts; zero Lexaloffle/BBS assets, so freely redistributable), copied
# to a release-named file. Build this LOCALLY: Calypsi is not redistributable
# into a public CI, so the release ROM is built here and attached to the
# GitHub release by hand. CI (.github/workflows) runs the host suites.
RELEASEROM ?= $(BUILD)/snespico8.sfc
.PHONY: release
release:
	$(MAKE) demo-rom
	cp $(BUILD)/p8.sfc $(RELEASEROM)
	@echo "==> Release ROM: $(RELEASEROM)"
	@echo "    Free public build: snespico8 runtime + our own demo carts."
	@echo "    Attach this to a GitHub release (see docs/PROJECT.md M14)."

# ---- M9 T0: measurement --------------------------------------------
# Each bench builds the default ROM around a cart and runs test/p8bench.lua,
# which samples the live MB_VMOPS/MB_FLIPS mailbox counters once a second
# (of emulated time) and prints per-window slopes; the baseline table lives
# in docs/M9-perf-plan.md. FRAMES is huge: the script decides when to stop.
BENCHCART ?= ../core-c/tests/carts/bench_vm.p8
.PHONY: bench bench-vm bench-draw bench-flip bench-api bench-celeste prof
bench:
	$(MAKE) p8 CART=$(BENCHCART) FRAMES=1000000000
	$(MESEN) --testrunner --timeout=900 $(BUILD)/p8.sfc test/p8bench.lua

bench-vm:
	$(MAKE) bench BENCHCART=../core-c/tests/carts/bench_vm.p8
bench-draw:
	$(MAKE) bench BENCHCART=../core-c/tests/carts/bench_draw.p8
bench-flip:
	$(MAKE) bench BENCHCART=../core-c/tests/carts/bench_flip.p8
bench-api:
	$(MAKE) bench BENCHCART=../core-c/tests/carts/demo_video.p8
bench-celeste:
	$(MAKE) p8 CART=../core-c/tests/carts-external/celeste.p8 FRAMES=1000000000
	$(MESENLONG) $(BUILD)/p8.sfc test/p8bench.lua

# PC-sampling profile of whatever ROM is in build/ (run right after a bench
# target so the ROM matches): one emu.getState() PC sample per frame, then
# tools/profsym.py folds the histogram through build/p8.map symbols.
prof:
	$(MESEN) --testrunner --timeout=900 $(BUILD)/p8.sfc test/p8prof.lua | tee $(BUILD)/prof_raw.txt
	cp $(BUILD)/p8.map $(BUILD)/prof.map   # static ?L numbers shift per build
	python3 tools/profsym.py $(BUILD)/prof.map $(BUILD)/prof_raw.txt

# M9 T5: on-target A/B -- hardware math units vs the pure-C references
# over random + directed corpora (the pxa discipline: asm-tier code is
# trusted only after bit-identity on target). ~3-4 min wall.
.PHONY: test-abmath
test-abmath:
	rm -f $(BUILD)/p8obj/p8main.o
	$(MAKE) p8 P8MAINDEF=-DP8_ABMATH
	$(MESENLONG) $(BUILD)/p8.sfc test/abmath.lua | grep -a "RESULT PASS abmath"
	rm -f $(BUILD)/p8obj/p8main.o

# M5c: boot Celeste Classic on the SNES. The cart is CC-BY-NC-SA and not
# committed; fetch it first (same as core-c/tests/celeste.sh):
#   curl -sL -o ../core-c/tests/carts-external/celeste.p8 \
#     https://raw.githubusercontent.com/CelesteClassic/evercore/main/evercore.p8
# Slow: ~1 min emulated to parse 27KB of Lua, then a few gameplay frames.
.PHONY: test-celeste
test-celeste:
	$(MAKE) p8 CART=../core-c/tests/carts-external/celeste.p8
	$(MESENLONG) $(BUILD)/p8.sfc test/celeste.lua

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
