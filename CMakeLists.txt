cmake_minimum_required(VERSION 3.10)
project(cocoemu C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -pedantic)
endif()

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

find_package(SDL2 QUIET)
if(SDL2_FOUND)
  add_executable(coco-sdl frontend/sdl/main.c)
  if(TARGET SDL2::SDL2)
    target_link_libraries(coco-sdl PRIVATE cocomedia SDL2::SDL2)
    if(TARGET SDL2::SDL2main)
      target_link_libraries(coco-sdl PRIVATE SDL2::SDL2main)
    endif()
  else()
    target_include_directories(coco-sdl PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(coco-sdl PRIVATE cocomedia ${SDL2_LIBRARIES})
  endif()
else()
  message(STATUS "SDL2 nao encontrado: frontend coco-sdl nao sera compilado")
endif()

enable_testing()
add_executable(test_cpu tests/test_cpu.c)
target_include_directories(test_cpu PRIVATE src/core)
target_link_libraries(test_cpu PRIVATE cococore)
add_test(NAME cpu COMMAND test_cpu)

add_executable(test_fdc tests/test_fdc.c)
target_include_directories(test_fdc PRIVATE src/core)
target_link_libraries(test_fdc PRIVATE cococore)
add_test(NAME fdc COMMAND test_fdc)
