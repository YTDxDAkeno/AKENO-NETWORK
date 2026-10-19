# AKENO Party Voice, PC build: the service against a simulated console (unit tests and the
# end-to-end test with the real server, LiveKit and the voice gateway). The PS5 build is
# console/Makefile. A PC run is never evidence that anything works on a PS5.
#
#   clients/ps5-native/scripts/fetch-deps.sh     # libopus, mbedTLS, CA bundle, stb_image (checksummed)
#   cmake -S clients/ps5-partyvoice -B clients/ps5-partyvoice/build -DPV_SANITIZE=ON
#   cmake --build clients/ps5-partyvoice/build && ctest --test-dir clients/ps5-partyvoice/build
cmake_minimum_required(VERSION 3.16)
project(akeno_partyvoice C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(NATIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5-native")
set(PS5_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5")
set(LAB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5-voicelab")
set(NATIVE_TP "${NATIVE_DIR}/third_party")
set(AKENO_TP "${PS5_DIR}/third_party")

option(PV_SANITIZE "AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(PV_TSAN "ThreadSanitizer" OFF)
foreach(input "${NATIVE_TP}/opus/include/opus.h" "${NATIVE_TP}/stb_image.h" "${AKENO_TP}/mbedtls/library/ssl_tls.c" "${AKENO_TP}/cacert.pem")
  if(NOT EXISTS "${input}")
    message(FATAL_ERROR "${input} is missing; run clients/ps5-native/scripts/fetch-deps.sh")
  endif()
endforeach()

set(PV_WARNINGS -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith -Werror)
if(PV_SANITIZE)
  add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()
if(PV_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()
find_package(Threads REQUIRED)

# Third-party code, unchanged: mbedTLS (the console transport, as on the PS5), libopus 1.5.2, stb_image.
file(GLOB PV_MBEDTLS_SOURCES "${AKENO_TP}/mbedtls/library/*.c")
add_library(pv_mbedtls STATIC ${PV_MBEDTLS_SOURCES})
target_include_directories(pv_mbedtls SYSTEM PUBLIC "${AKENO_TP}/mbedtls/include" "${PS5_DIR}/config")
target_compile_definitions(pv_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="akeno_mbedtls_config.h")
target_compile_options(pv_mbedtls PRIVATE -w)

file(STRINGS "${NATIVE_DIR}/cmake/opus_sources.txt" PV_OPUS_SOURCES)
list(TRANSFORM PV_OPUS_SOURCES PREPEND "${NATIVE_TP}/opus/")
add_library(pv_opus STATIC ${PV_OPUS_SOURCES})
target_include_directories(pv_opus PUBLIC "${NATIVE_TP}/opus/include"
  PRIVATE "${NATIVE_TP}/opus" "${NATIVE_TP}/opus/celt" "${NATIVE_TP}/opus/silk" "${NATIVE_TP}/opus/silk/float")
target_compile_definitions(pv_opus PRIVATE OPUS_BUILD VAR_ARRAYS HAVE_LRINT HAVE_LRINTF "PACKAGE_VERSION=\"1.5.2\"")
target_compile_options(pv_opus PRIVATE -w -O2)
target_link_libraries(pv_opus PUBLIC m)

add_library(pv_stbimage STATIC "${NATIVE_DIR}/src/third/stb_image_impl.c")
target_include_directories(pv_stbimage PRIVATE "${NATIVE_TP}" "${NATIVE_DIR}/src/core")
target_compile_options(pv_stbimage PRIVATE -w)

# Reused AKENO code, unchanged: the daemon's TLS/WebSocket client and local HTTP parser, AKENO
# Native's voice core and daemon client, the Voice Lab's PCM helpers.
add_library(pv_shared STATIC
  "${PS5_DIR}/src/buf.c" "${PS5_DIR}/src/json.c" "${PS5_DIR}/src/json_write.c" "${PS5_DIR}/src/crypto_util.c" "${PS5_DIR}/src/url.c"
  "${PS5_DIR}/src/http.c" "${PS5_DIR}/src/ws.c" "${PS5_DIR}/src/net_posix.c" "${PS5_DIR}/src/transport_mbedtls.c" "${PS5_DIR}/app/ui_http.c"
  "${NATIVE_DIR}/src/core/voice.c" "${NATIVE_DIR}/src/core/voice_codec.c" "${NATIVE_DIR}/src/core/voice_dsp.c"
  "${NATIVE_DIR}/src/core/voice_proto.c" "${NATIVE_DIR}/src/core/jitter.c" "${NATIVE_DIR}/src/core/account.c"
  "${NATIVE_DIR}/src/core/social.c" "${NATIVE_DIR}/src/core/chat.c" "${NATIVE_DIR}/src/core/image.c" "${NATIVE_DIR}/src/core/text.c"
  "${NATIVE_DIR}/src/app/ipc.c" "${LAB_DIR}/src/lab_pcm.c" "${NATIVE_DIR}/src/core/context.c" "${PS5_DIR}/app/game_detect.c")
target_include_directories(pv_shared PUBLIC "${PS5_DIR}/include" "${PS5_DIR}/src" "${PS5_DIR}/app" "${NATIVE_DIR}/src/core"
  "${NATIVE_DIR}/src/app" "${LAB_DIR}/src")
target_compile_definitions(pv_shared PUBLIC AKENO_NATIVE_IPC
  PRIVATE "AKENO_CA_BUNDLE_PATH=\"${AKENO_TP}/cacert.pem\"" "AKENO_CA_BUNDLE_NAME=\"Mozilla CA bundle from certifi 2026.07.22\"")
set_source_files_properties("${PS5_DIR}/src/transport_mbedtls.c" PROPERTIES OBJECT_DEPENDS "${AKENO_TP}/cacert.pem")
target_compile_options(pv_shared PRIVATE -Wall -Wextra)
target_link_libraries(pv_shared PUBLIC pv_mbedtls pv_opus pv_stbimage Threads::Threads m)
target_link_libraries(pv_mbedtls PUBLIC pv_shared) # mbedtls_hardware_poll lives in transport_mbedtls.c

execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE PV_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT PV_GIT_COMMIT)
  set(PV_GIT_COMMIT "dev")
endif()

add_library(pv_core STATIC src/pv_engine.c src/pv_net.c src/pv_audio.c src/pv_status.c src/pv_control.c src/pv_backend_host.c)
target_include_directories(pv_core PUBLIC src)
target_compile_options(pv_core PRIVATE ${PV_WARNINGS})
target_compile_definitions(pv_core PUBLIC "PV_BUILD_ID=\"${PV_GIT_COMMIT}-pc\"")
target_link_libraries(pv_core PUBLIC pv_shared)

add_executable(akeno-party-voice src/pv_main.c)
target_compile_options(akeno-party-voice PRIVATE ${PV_WARNINGS})
target_link_libraries(akeno-party-voice PRIVATE pv_core)

enable_testing()
add_executable(pv_unit_tests tests/test_partyvoice.c)
target_compile_options(pv_unit_tests PRIVATE ${PV_WARNINGS})
target_link_libraries(pv_unit_tests PRIVATE pv_core)
add_test(NAME partyvoice COMMAND pv_unit_tests)
