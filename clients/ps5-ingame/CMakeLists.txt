cmake_minimum_required(VERSION 3.16)
project(akeno_ingame LANGUAGES C)

# PC build of the EXPERIMENTAL AKENO In-Game Companion (docs/IN_GAME_ACCESS.md): unit tests of the
# menu model and of the OnionHEN page, and a check tool that loads the menu from a running AKENO
# daemon. The PS5 plugin ELF is built by console/Makefile. Nothing here is evidence about OnionHEN
# or a PS5.
#
#   clients/ps5-ingame/scripts/fetch-deps.sh
#   cmake -S clients/ps5-ingame -B clients/ps5-ingame/build -DAKN_SANITIZE=ON && cmake --build clients/ps5-ingame/build
#   ctest --test-dir clients/ps5-ingame/build

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(AKN_SANITIZE "AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5-native")
set(PS5_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5")
set(ONION_SDK "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onion-sdk")
if(NOT EXISTS "${ONION_SDK}/runtime/ui.c" OR NOT EXISTS "${NATIVE_DIR}/third_party/stb_image.h")
  message(FATAL_ERROR "Run clients/ps5-ingame/scripts/fetch-deps.sh first")
endif()

set(AKN_WARNINGS -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror)
if(AKN_SANITIZE)
  add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()
find_package(Threads REQUIRED)

# The OnionHEN plugin SDK runtime (GPLv3), unmodified, at the pinned commit.
file(GLOB ONION_RUNTIME "${ONION_SDK}/runtime/*.c")
add_library(onion_runtime STATIC ${ONION_RUNTIME})
target_include_directories(onion_runtime PUBLIC "${ONION_SDK}/include" PRIVATE "${ONION_SDK}/runtime")
target_compile_definitions(onion_runtime PRIVATE _DEFAULT_SOURCE)
target_link_libraries(onion_runtime PUBLIC Threads::Threads)

# AKENO Native's parsers and IPC client, unchanged (the same code the native app runs).
add_library(akn_shared STATIC
  "${NATIVE_DIR}/src/core/social.c" "${NATIVE_DIR}/src/core/chat.c" "${NATIVE_DIR}/src/core/party.c"
  "${NATIVE_DIR}/src/core/account.c" "${NATIVE_DIR}/src/core/image.c" "${NATIVE_DIR}/src/core/text.c"
  "${NATIVE_DIR}/src/third/stb_image_impl.c" "${NATIVE_DIR}/src/app/ipc.c"
  "${PS5_DIR}/src/json.c" "${PS5_DIR}/src/buf.c")
target_include_directories(akn_shared PUBLIC "${NATIVE_DIR}/src/core" "${NATIVE_DIR}/src/app" "${PS5_DIR}/include"
  PRIVATE "${PS5_DIR}/src" "${NATIVE_DIR}/third_party")
target_link_libraries(akn_shared PUBLIC m)

add_library(akn_ingame STATIC src/companion.c src/fetch.c src/ui_doc.c src/actions.c)
target_include_directories(akn_ingame PUBLIC src)
target_compile_options(akn_ingame PRIVATE ${AKN_WARNINGS})
target_link_libraries(akn_ingame PUBLIC akn_shared onion_runtime)

add_executable(akeno-ingame-check src/check_main.c)
target_compile_options(akeno-ingame-check PRIVATE ${AKN_WARNINGS})
target_link_libraries(akeno-ingame-check PRIVATE akn_ingame)

enable_testing()
add_executable(akn_ingame_tests tests/test_companion.c)
target_compile_options(akn_ingame_tests PRIVATE ${AKN_WARNINGS})
target_link_libraries(akn_ingame_tests PRIVATE akn_ingame)
add_test(NAME ingame COMMAND akn_ingame_tests)
