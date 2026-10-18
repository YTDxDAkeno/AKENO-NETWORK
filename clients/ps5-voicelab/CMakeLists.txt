# AKENO Voice Lab, PC build: the lab logic against a simulated console, with unit tests.
# The PS5 build is console/Makefile.
cmake_minimum_required(VERSION 3.16)
project(akeno_voicelab C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(PS5_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ps5)

option(AKN_LAB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(AKN_LAB_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(-Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror)

find_package(Threads REQUIRED)

add_library(akn_lab STATIC src/lab.c src/lab_pcm.c src/lab_net.c src/backend_host.c ${PS5_DIR}/src/json.c ${PS5_DIR}/src/buf.c)
target_include_directories(akn_lab PUBLIC src ${PS5_DIR}/include ${PS5_DIR}/src)
target_link_libraries(akn_lab PUBLIC Threads::Threads m)

add_executable(akeno-voicelab src/main.c)
target_link_libraries(akeno-voicelab akn_lab)

enable_testing()
add_executable(akn_lab_tests tests/test_lab.c)
target_link_libraries(akn_lab_tests akn_lab)
add_test(NAME voicelab COMMAND akn_lab_tests)
