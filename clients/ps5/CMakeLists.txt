cmake_minimum_required(VERSION 3.16)
project(akeno_device_client VERSION 0.1.0 LANGUAGES C)

# Host build (Linux) of the AKENO NETWORK device client.
#
# Two transports are built from the same client sources:
#   akeno_client          POSIX sockets + OpenSSL (system library)
#   akeno_client_mbedtls  POSIX sockets + mbedTLS, i.e. the transport of the PS5 build, so the
#                         whole test suite also runs against the console TLS code. Requires the
#                         pinned inputs from scripts/fetch-deps.sh.
# The PS5 executable itself is built with ps5/Makefile and the PS5 payload SDK.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(AKENO_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AKENO_WERROR "Treat warnings as errors" ON)

find_package(OpenSSL 1.1.1 REQUIRED)

set(AKENO_WARNINGS -Wall -Wextra -Wshadow -Wformat=2 -Wconversion -Wno-sign-conversion -Wstrict-prototypes -Wvla)
if(AKENO_WERROR)
  list(APPEND AKENO_WARNINGS -Werror)
endif()
set(AKENO_HARDENING -fstack-protector-strong -D_FORTIFY_SOURCE=2)
if(AKENO_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
  set(AKENO_HARDENING -fstack-protector-strong)
endif()

# Platform-neutral core (no sockets, no TLS).
add_library(akeno_core STATIC
  src/buf.c src/json.c src/json_write.c src/crypto_util.c src/url.c src/backoff.c src/caps.c src/http.c src/ws.c)
target_include_directories(akeno_core PUBLIC include PRIVATE src)
target_compile_options(akeno_core PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno_core PUBLIC m)

# Client with the POSIX + OpenSSL transport.
add_library(akeno_client STATIC src/client.c src/realtime.c src/net_posix.c src/transport_openssl.c)
target_include_directories(akeno_client PUBLIC include PRIVATE src)
target_compile_options(akeno_client PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno_client PUBLIC akeno_core OpenSSL::SSL OpenSSL::Crypto)

add_executable(akeno-cli cli/main.c)
target_compile_options(akeno-cli PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno-cli PRIVATE akeno_client)

# Client with the POSIX + mbedTLS transport (the console transport).
set(AKENO_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "Output of scripts/fetch-deps.sh")
option(AKENO_REQUIRE_MBEDTLS "Fail when the mbedTLS inputs are missing" OFF)
if(EXISTS "${AKENO_THIRD_PARTY_DIR}/mbedtls/library/ssl_tls.c" AND EXISTS "${AKENO_THIRD_PARTY_DIR}/cacert.pem")
  set(AKENO_HAVE_MBEDTLS ON)
elseif(AKENO_REQUIRE_MBEDTLS)
  message(FATAL_ERROR "mbedTLS inputs missing; run clients/ps5/scripts/fetch-deps.sh")
else()
  message(STATUS "mbedTLS inputs not found; skipping the mbedTLS transport (run scripts/fetch-deps.sh)")
endif()

if(AKENO_HAVE_MBEDTLS)
  find_package(Threads REQUIRED)
  file(GLOB AKENO_MBEDTLS_SOURCES "${AKENO_THIRD_PARTY_DIR}/mbedtls/library/*.c")
  add_library(akeno_mbedtls STATIC ${AKENO_MBEDTLS_SOURCES})
  target_include_directories(akeno_mbedtls SYSTEM PUBLIC "${AKENO_THIRD_PARTY_DIR}/mbedtls/include" config)
  target_compile_definitions(akeno_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="akeno_mbedtls_config.h")
  target_link_libraries(akeno_mbedtls PUBLIC Threads::Threads)

  add_library(akeno_client_mbedtls STATIC src/client.c src/realtime.c src/net_posix.c src/transport_mbedtls.c)
  target_include_directories(akeno_client_mbedtls PUBLIC include PRIVATE src)
  target_compile_options(akeno_client_mbedtls PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
  target_compile_definitions(akeno_client_mbedtls PRIVATE
    "AKENO_CA_BUNDLE_PATH=\"${AKENO_THIRD_PARTY_DIR}/cacert.pem\""
    "AKENO_CA_BUNDLE_NAME=\"Mozilla CA bundle from certifi 2026.07.22\"")
  set_source_files_properties(src/transport_mbedtls.c PROPERTIES OBJECT_DEPENDS "${AKENO_THIRD_PARTY_DIR}/cacert.pem")
  target_link_libraries(akeno_client_mbedtls PUBLIC akeno_core akeno_mbedtls)

  add_executable(akeno-cli-mbedtls cli/main.c)
  target_compile_options(akeno-cli-mbedtls PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
  target_link_libraries(akeno-cli-mbedtls PRIVATE akeno_client_mbedtls)

  # The console app, built for Linux with the console transport (the PS5 build uses ps5/Makefile).
  execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE AKENO_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT AKENO_GIT_COMMIT)
    set(AKENO_GIT_COMMIT "dev")
  endif()
  set(AKENO_APP_SOURCES app/app.c app/app_routes.c app/activity.c app/game_detect.c app/quick_access.c app/shortcut.c app/ui_server.c
    app/ui_http.c app/ui_assets.c app/shareplay_rx.c app/account_bridge.c app/app_account.c)
  add_executable(akeno-app app/main.c app/platform_host.c app/platform_host_account.c ${AKENO_APP_SOURCES})
  target_include_directories(akeno-app PRIVATE src app)
  target_compile_options(akeno-app PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
  target_compile_definitions(akeno-app PRIVATE
    "AKENO_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/app/web\""
    "AKENO_BUILD_ID=\"${AKENO_GIT_COMMIT}-linux\"")
  set_source_files_properties(app/ui_assets.c PROPERTIES OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/web/index.html;${CMAKE_CURRENT_SOURCE_DIR}/app/web/app.js;${CMAKE_CURRENT_SOURCE_DIR}/app/web/app.css;${CMAKE_CURRENT_SOURCE_DIR}/app/web/shareplay.js;${CMAKE_CURRENT_SOURCE_DIR}/app/web/audiolab.js")
  target_link_libraries(akeno-app PRIVATE akeno_client_mbedtls)

  # Audio lab: the same app with the experimental PS5 audio research (simulated on Linux).
  add_executable(akeno-app-audiolab app/main.c app/platform_host.c app/platform_host_account.c app/audio_lab.c app/audio_lab_pcm.c app/audio_lab_state.c ${AKENO_APP_SOURCES})
  target_include_directories(akeno-app-audiolab PRIVATE src app)
  target_compile_options(akeno-app-audiolab PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
  target_compile_definitions(akeno-app-audiolab PRIVATE AKENO_EXPERIMENTAL_AUDIO
    "AKENO_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/app/web\""
    "AKENO_BUILD_ID=\"${AKENO_GIT_COMMIT}-linux-audiolab\"")
  target_link_libraries(akeno-app-audiolab PRIVATE akeno_client_mbedtls)

  # Native IPC: the same app with the local interface for AKENO Native (docs/PS5_NATIVE_PHASE2.md).
  add_executable(akeno-app-native-ipc app/main.c app/platform_host.c app/platform_host_account.c ${AKENO_APP_SOURCES})
  target_include_directories(akeno-app-native-ipc PRIVATE src app)
  target_compile_options(akeno-app-native-ipc PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
  target_compile_definitions(akeno-app-native-ipc PRIVATE AKENO_NATIVE_IPC
    "AKENO_UI_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/app/web\""
    "AKENO_BUILD_ID=\"${AKENO_GIT_COMMIT}-linux-native-ipc\"")
  target_link_libraries(akeno-app-native-ipc PRIVATE akeno_client_mbedtls)
endif()

enable_testing()
add_executable(akeno_unit_tests tests/unit/test_main.c tests/unit/test_core.c tests/unit/test_app.c tests/unit/test_game.c
  tests/unit/test_shareplay.c tests/unit/test_account_bridge.c tests/unit/test_shortcut.c tests/unit/test_audio_pcm.c tests/unit/test_audio_state.c
  app/ui_http.c app/game_detect.c app/shareplay_rx.c app/account_bridge.c app/shortcut.c app/audio_lab_pcm.c app/audio_lab_state.c)
target_include_directories(akeno_unit_tests PRIVATE src app)
target_compile_options(akeno_unit_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(akeno_unit_tests PRIVATE akeno_client)
add_test(NAME unit COMMAND akeno_unit_tests)
if(AKENO_HAVE_MBEDTLS)
  add_executable(akeno_unit_tests_mbedtls tests/unit/test_main.c tests/unit/test_core.c tests/unit/test_app.c tests/unit/test_game.c
    tests/unit/test_shareplay.c tests/unit/test_account_bridge.c tests/unit/test_shortcut.c tests/unit/test_audio_pcm.c tests/unit/test_audio_state.c
    app/ui_http.c app/game_detect.c app/shareplay_rx.c app/account_bridge.c app/shortcut.c app/audio_lab_pcm.c app/audio_lab_state.c)
  target_include_directories(akeno_unit_tests_mbedtls PRIVATE src app)
  target_compile_options(akeno_unit_tests_mbedtls PRIVATE -Wall -Wextra -Werror)
  target_link_libraries(akeno_unit_tests_mbedtls PRIVATE akeno_client_mbedtls)
  add_test(NAME unit-mbedtls COMMAND akeno_unit_tests_mbedtls)
endif()
