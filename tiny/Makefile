# Makefile - coco4k: emulador CoCo 1/2 em um executavel Windows de 4 KB
#
#   make            monta build/tiny/coco4k.exe e confere o limite de 4096 bytes
#   make safe       variante com cabecalho PE convencional (maior que 4 KB)
#   make test       testes diferenciais contra o emulador em C
#                   (Linux/MSYS2 com gcc -m32 e python3)
#   make font       regenera font.inc a partir de ../src/core/font6847.c

NASM   ?= nasm
CC32   ?= gcc -m32 -no-pie
PYTHON ?= python3
B       = ../build/tiny
LIMIT   = 4096

.PHONY: all safe test font clean

all: $(B)/coco4k.exe
	@size=$$(wc -c < $<); \
	echo "coco4k.exe: $$size bytes (limite $(LIMIT))"; \
	test $$size -le $(LIMIT) || { echo "ERRO: passou de $(LIMIT) bytes"; exit 1; }

$(B)/coco4k.exe: coco4k.asm core.inc font.inc
	@mkdir -p $(B)
	$(NASM) -f bin -o $@ coco4k.asm

safe: coco4k.asm core.inc font.inc
	@mkdir -p $(B)
	$(NASM) -f bin -DSAFE_HEADER -o $(B)/coco4k_safe.exe coco4k.asm
	@echo "coco4k_safe.exe: $$(wc -c < $(B)/coco4k_safe.exe) bytes"

font:
	$(PYTHON) tools/mkfont.py ../src/core/font6847.c font.inc

# --- testes -------------------------------------------------------------------
C32OBJ = $(patsubst ../src/core/%.c,$(B)/c32/%.o,$(wildcard ../src/core/*.c))

$(B)/c32/%.o: ../src/core/%.c
	@mkdir -p $(B)/c32
	$(CC32) -O2 -w -I../include -I../src/core -c $< -o $@

$(B)/cpu_elf.o: test/cpu_elf.asm core.inc
	@mkdir -p $(B)
	$(NASM) -f elf32 -DCPU_ONLY -I./ -o $@ $<

$(B)/mach_elf.o: test/cpu_elf.asm core.inc font.inc
	@mkdir -p $(B)
	$(NASM) -f elf32 -I./ -o $@ $<

$(B)/difftest: test/difftest.c $(B)/cpu_elf.o $(B)/c32/mc6809.o
	$(CC32) -O2 -w -I../src/core $^ -o $@ 2>/dev/null || $(CC32) -O2 -w -I../src/core $^ -o $@

$(B)/machtest: test/machtest.c $(B)/mach_elf.o $(C32OBJ)
	$(CC32) -O2 -w -I../include $^ -o $@ 2>/dev/null || $(CC32) -O2 -w -I../include $^ -o $@

test: $(B)/difftest $(B)/machtest
	$(B)/difftest 2000000 1
	$(B)/difftest 2000000 2
	$(PYTHON) ../tests/make_test_rom.py $(B)/testrom.rom
	$(B)/machtest $(B)/testrom.rom
	$(PYTHON) ../tools/asm6809.py ../examples/game/game.asm $(B)/game.bin
	$(B)/machtest $(B)/game.bin cart
	$(PYTHON) test/gfxroms.py $(B)
	@for g in 0 1 2 3 4 5 6 7; do for c in 0 1; do \
	  printf "modo grafico %d, CSS %d: " $$g $$c; \
	  $(B)/machtest $(B)/gfx$$g$$c.rom | tail -1 | grep " 0 pixels" || exit 1; \
	done; done

clean:
	rm -rf $(B)
