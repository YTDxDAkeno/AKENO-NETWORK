# AKENO System Explorer, PC build: the diagnostic logic against a simulated console, with unit
# tests. The PS5 build is console/Makefile.
cmake_minimum_required(VERSION 3.16)
project(akeno_system_explorer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(AKN_EX_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(AKN_EX_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(-Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror)

add_library(akn_ex STATIC
  src/ex.c
  src/ex_process.c
  src/ex_libraries.c
  src/ex_audio.c
  src/ex_shellui.c
  src/ex_timeline.c
  src/ex_report.c
  src/ex_redact.c
  src/ex_backend_host.c)
target_include_directories(akn_ex PUBLIC src)

add_executable(akeno-system-explorer src/main.c)
target_link_libraries(akeno-system-explorer akn_ex)

enable_testing()
add_executable(akn_ex_tests tests/test_explorer.c)
target_link_libraries(akn_ex_tests akn_ex)
add_test(NAME explorer COMMAND akn_ex_tests)
