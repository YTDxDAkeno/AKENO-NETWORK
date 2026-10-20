# AKENO Audio Lab 3.1, PC build: the portable logic + the simulated console, with unit tests. The
# PS5 build is console/Makefile. A passing PC run is never evidence of PS5 behaviour.
cmake_minimum_required(VERSION 3.16)
project(akeno_audio_lab C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(AL_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(AL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(-Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror)

find_package(Threads REQUIRED)

add_library(akn_al STATIC
  src/al.c
  src/al_engine.c
  src/al_report.c
  src/audio_lab_pcm.c
  src/audio_lab_state.c
  src/al_backend_host.c)
target_include_directories(akn_al PUBLIC src)

add_executable(akeno-audio-lab src/main.c)
target_link_libraries(akeno-audio-lab akn_al Threads::Threads)

enable_testing()
add_executable(akn_al_tests tests/test_audiolab.c)
target_link_libraries(akn_al_tests akn_al Threads::Threads)
add_test(NAME audiolab COMMAND akn_al_tests)
