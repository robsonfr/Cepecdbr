cmake_minimum_required(VERSION 3.10)
project(cocoemu C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -pedantic)
endif()

# Jogo embarcado (veja o Makefile para a descricao)
set(GAME_ASM   "${CMAKE_SOURCE_DIR}/examples/game/game.asm" CACHE FILEPATH "Fonte assembly do jogo")
set(GAME_BIN   "" CACHE FILEPATH "Binario do jogo ja montado (opcional; substitui GAME_ASM)")
set(GAME_TITLE "Coleta" CACHE STRING "Titulo da janela do jogo")
set(GAME_NAME  "coleta" CACHE STRING "Nome do executavel do jogo")

# Nucleo: C99 puro, sem dependencias externas
add_library(cococore STATIC
  src/core/mc6809.c
  src/core/mc6821.c
  src/core/sam.c
  src/core/vdg.c
  src/core/font6847.c
  src/core/wd1793.c
  src/core/cassette.c
  src/core/coco.c)
target_include_directories(cococore PUBLIC include PRIVATE src/core)

# Midia baseada em arquivos (ROMs, JVC/DSK, CAS) compartilhada pelos frontends
add_library(cocomedia STATIC frontend/common/media.c)
target_include_directories(cocomedia PUBLIC frontend/common)
target_link_libraries(cocomedia PUBLIC cococore)

add_executable(coco-headless frontend/headless/main.c)
target_link_libraries(coco-headless PRIVATE cocomedia)

find_package(Python3 COMPONENTS Interpreter QUIET)

# Binario do jogo: GAME_BIN ou montado a partir de GAME_ASM
if(GAME_BIN)
  set(GAME_BIN_FILE "${GAME_BIN}")
elseif(Python3_FOUND)
  set(GAME_BIN_FILE "${CMAKE_BINARY_DIR}/game.bin")
  add_custom_command(
    OUTPUT ${GAME_BIN_FILE}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/asm6809.py
            ${GAME_ASM} ${GAME_BIN_FILE} ${CMAKE_BINARY_DIR}/game.lst
    DEPENDS ${GAME_ASM} ${CMAKE_SOURCE_DIR}/tools/asm6809.py
    COMMENT "Montando ${GAME_ASM}")
endif()

add_executable(bin2c tools/bin2c.c)

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_library(cocosdl STATIC frontend/sdl/sdl_app.c)
  target_include_directories(cocosdl PUBLIC frontend/sdl)
  if(TARGET SDL2::SDL2)
    target_link_libraries(cocosdl PUBLIC cocomedia SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(cocosdl PUBLIC SDL2::SDL2main)
    endif()
  else()
    target_include_directories(cocosdl PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(cocosdl PUBLIC cocomedia ${SDL2_LIBRARIES})
  endif()

  add_executable(coco-sdl frontend/sdl/main.c)
  target_link_libraries(coco-sdl PRIVATE cocosdl)

  if(GAME_BIN_FILE)
    add_custom_command(
      OUTPUT ${CMAKE_BINARY_DIR}/game_rom.c
      COMMAND bin2c ${GAME_BIN_FILE} ${CMAKE_BINARY_DIR}/game_rom.c "${GAME_TITLE}" ${GAME_NAME}
      DEPENDS bin2c ${GAME_BIN_FILE}
      COMMENT "Embarcando ${GAME_BIN_FILE}")
    add_executable(${GAME_NAME} frontend/game/main.c ${CMAKE_BINARY_DIR}/game_rom.c)
    target_link_libraries(${GAME_NAME} PRIVATE cocosdl)
  endif()
else()
  message(STATUS "SDL2 nao encontrado: coco-sdl e o jogo nao serao compilados")
endif()

enable_testing()
foreach(t cpu fdc)
  add_executable(test_${t} tests/test_${t}.c)
  target_include_directories(test_${t} PRIVATE src/core)
  target_link_libraries(test_${t} PRIVATE cococore)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

if(Python3_FOUND)
  add_executable(test_machine tests/test_machine.c)
  target_link_libraries(test_machine PRIVATE cococore)
  add_test(NAME testrom
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/make_test_rom.py ${CMAKE_BINARY_DIR}/testrom.rom)
  add_test(NAME machine COMMAND test_machine ${CMAKE_BINARY_DIR}/testrom.rom)
  set_tests_properties(machine PROPERTIES DEPENDS testrom)

  add_executable(test_game tests/test_game.c)
  target_link_libraries(test_game PRIVATE cococore)
  add_test(NAME gamebin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/asm6809.py
            ${CMAKE_SOURCE_DIR}/examples/game/game.asm ${CMAKE_BINARY_DIR}/test_game.bin)
  add_test(NAME game COMMAND test_game ${CMAKE_BINARY_DIR}/test_game.bin)
  set_tests_properties(game PROPERTIES DEPENDS gamebin)
endif()
