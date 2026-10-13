# Makefile - Emulador TRS-80 Color Computer 1/2
#
#   make            compila o nucleo, o frontend SDL2 e o headless
#   make sdl        somente o frontend SDL2  (requer sdl2-config)
#   make headless   somente o frontend sem interface
#   make test       compila e roda os testes (CPU, FDC e maquina com ROM de teste)

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -pedantic
CPPFLAGS += -Iinclude -Isrc/core -Ifrontend/common

SDL_CONFIG ?= sdl2-config
SDL_CFLAGS  = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS    = $(shell $(SDL_CONFIG) --libs)

BUILD = build

CORE_SRC = src/core/mc6809.c src/core/mc6821.c src/core/sam.c \
           src/core/vdg.c src/core/font6847.c src/core/wd1793.c \
           src/core/cassette.c src/core/coco.c
COMMON_SRC = frontend/common/media.c

CORE_OBJ   = $(CORE_SRC:%.c=$(BUILD)/%.o)
COMMON_OBJ = $(COMMON_SRC:%.c=$(BUILD)/%.o)

LIBCORE = $(BUILD)/libcococore.a

.PHONY: all sdl headless test clean

all: sdl headless

$(LIBCORE): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/frontend/sdl/%.o: frontend/sdl/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

sdl: $(BUILD)/coco-sdl
headless: $(BUILD)/coco-headless

$(BUILD)/coco-sdl: $(BUILD)/frontend/sdl/main.o $(COMMON_OBJ) $(LIBCORE)
	$(CC) $(CFLAGS) $^ $(SDL_LIBS) -o $@

$(BUILD)/coco-headless: $(BUILD)/frontend/headless/main.o $(COMMON_OBJ) $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_cpu: $(BUILD)/tests/test_cpu.o $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_fdc: $(BUILD)/tests/test_fdc.o $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_machine: $(BUILD)/tests/test_machine.o $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILD)/test_cpu $(BUILD)/test_fdc $(BUILD)/test_machine
	$(BUILD)/test_cpu
	$(BUILD)/test_fdc
	python3 tests/make_test_rom.py $(BUILD)/testrom.rom
	$(BUILD)/test_machine $(BUILD)/testrom.rom

clean:
	rm -rf $(BUILD)

# dependencias de cabecalhos
$(CORE_OBJ) $(COMMON_OBJ): include/coco.h $(wildcard src/core/*.h) $(wildcard frontend/common/*.h)
