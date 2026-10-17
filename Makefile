# Makefile - Emulador TRS-80 Color Computer 1/2
#
#   make            compila o nucleo, o emulador SDL2, o headless e o jogo-exemplo
#   make sdl        somente o emulador SDL2 (requer sdl2-config)
#   make headless   somente o frontend sem interface
#   make game       executavel do jogo embarcado (sem ROMs)
#   make test       compila e roda os testes
#
# Jogo embarcado - variaveis:
#   GAME_ASM    fonte assembly montado com tools/asm6809.py
#               (padrao: examples/game/game.asm)
#   GAME_BIN    binario ja montado (ex.: com lwasm); se informado, GAME_ASM
#               e ignorado
#   GAME_TITLE  titulo da janela              (padrao: Coleta)
#   GAME_NAME   nome do executavel/snapshots  (padrao: coleta)
# Exemplo:
#   make game GAME_BIN=meujogo.bin GAME_TITLE="Meu Jogo" GAME_NAME=meujogo

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c99 -Wall -Wextra -pedantic
CPPFLAGS += -Iinclude -Isrc/core -Ifrontend/common
PYTHON  ?= python3

SDL_CONFIG ?= sdl2-config
SDL_CFLAGS  = $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS    = $(shell $(SDL_CONFIG) --libs)

ifeq ($(OS),Windows_NT)
EXE = .exe
endif

BUILD = build

CORE_SRC = src/core/mc6809.c src/core/mc6821.c src/core/sam.c \
           src/core/vdg.c src/core/font6847.c src/core/wd1793.c \
           src/core/cassette.c src/core/coco.c
COMMON_SRC = frontend/common/media.c

CORE_OBJ   = $(CORE_SRC:%.c=$(BUILD)/%.o)
COMMON_OBJ = $(COMMON_SRC:%.c=$(BUILD)/%.o)
SDLAPP_OBJ = $(BUILD)/frontend/sdl/sdl_app.o

LIBCORE = $(BUILD)/libcococore.a

GAME_ASM   ?= examples/game/game.asm
GAME_BIN   ?= $(BUILD)/game.bin
GAME_TITLE ?= Coleta
GAME_NAME  ?= coleta

.PHONY: all sdl headless game test clean FORCE

all: sdl headless game

$(LIBCORE): $(CORE_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/frontend/sdl/%.o: frontend/sdl/%.c frontend/sdl/sdl_app.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/frontend/game/%.o: frontend/game/%.c frontend/sdl/sdl_app.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -Ifrontend/sdl $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

sdl: $(BUILD)/coco-sdl$(EXE)
headless: $(BUILD)/coco-headless$(EXE)
game: $(BUILD)/$(GAME_NAME)$(EXE)

$(BUILD)/coco-sdl$(EXE): $(BUILD)/frontend/sdl/main.o $(SDLAPP_OBJ) $(COMMON_OBJ) $(LIBCORE)
	$(CC) $(CFLAGS) $^ $(SDL_LIBS) -o $@

$(BUILD)/coco-headless$(EXE): $(BUILD)/frontend/headless/main.o $(COMMON_OBJ) $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

# --- jogo embarcado ---------------------------------------------------------

$(BUILD)/bin2c$(EXE): tools/bin2c.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/game.bin: $(GAME_ASM) tools/asm6809.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/asm6809.py $(GAME_ASM) $@ $(BUILD)/game.lst

# conferido sempre (acompanha mudancas de GAME_BIN/GAME_TITLE), mas o arquivo
# so e substituido se o conteudo mudar, para nao religar o jogo a toa
$(BUILD)/game_rom.c: $(GAME_BIN) $(BUILD)/bin2c$(EXE) FORCE
	@$(BUILD)/bin2c$(EXE) $(GAME_BIN) $@.tmp "$(GAME_TITLE)" $(GAME_NAME) 2>/dev/null
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv -f $@.tmp $@; echo "embarcado: $(GAME_BIN) -> $@"; fi

$(BUILD)/game_rom.o: $(BUILD)/game_rom.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(GAME_NAME)$(EXE): $(BUILD)/frontend/game/main.o $(BUILD)/game_rom.o \
                             $(SDLAPP_OBJ) $(COMMON_OBJ) $(LIBCORE)
	$(CC) $(CFLAGS) $^ $(SDL_LIBS) -o $@

# --- testes -----------------------------------------------------------------

$(BUILD)/test_%$(EXE): $(BUILD)/tests/test_%.o $(LIBCORE)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BUILD)/test_cpu$(EXE) $(BUILD)/test_fdc$(EXE) $(BUILD)/test_machine$(EXE) \
      $(BUILD)/test_game$(EXE) $(BUILD)/game.bin
	$(BUILD)/test_cpu$(EXE)
	$(BUILD)/test_fdc$(EXE)
	$(PYTHON) tests/make_test_rom.py $(BUILD)/testrom.rom
	$(BUILD)/test_machine$(EXE) $(BUILD)/testrom.rom
	$(BUILD)/test_game$(EXE) $(BUILD)/game.bin

clean:
	rm -rf $(BUILD)

FORCE:

# dependencias de cabecalhos
$(CORE_OBJ) $(COMMON_OBJ): include/coco.h $(wildcard src/core/*.h) $(wildcard frontend/common/*.h)
$(BUILD)/frontend/sdl/main.o $(SDLAPP_OBJ) $(BUILD)/frontend/game/main.o: include/coco.h frontend/common/media.h
