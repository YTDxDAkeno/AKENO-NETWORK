cmake_minimum_required(VERSION 3.16)
project(akeno_native VERSION 0.1.0 LANGUAGES C)

# PC build of AKENO NETWORK Native (development and automated tests). The PS5 ELF is built by
# console/Makefile with the pinned PS5 payload SDK and SDL2 (see docs/PS5_NATIVE_CLIENT.md).
#
#   clients/ps5-native/scripts/fetch-deps.sh          # pinned stb_truetype, Inter, mbedTLS, CA bundle
#   cmake -S clients/ps5-native -B clients/ps5-native/build -DAKN_SANITIZE=ON
#   cmake --build clients/ps5-native/build && ctest --test-dir clients/ps5-native/build
#
# A desktop build is never evidence that anything works on a PS5.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(AKN_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(AKN_WERROR "Treat warnings as errors" ON)

set(PS5_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ps5")
set(AKN_THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "Output of scripts/fetch-deps.sh")
set(AKENO_THIRD_PARTY_DIR "${PS5_DIR}/third_party" CACHE PATH "mbedTLS and CA bundle (clients/ps5/scripts/fetch-deps.sh)")

foreach(input stb_truetype.h stb_image.h inter/Inter-Regular.ttf inter/Inter-Bold.ttf opus/include/opus.h)
  if(NOT EXISTS "${AKN_THIRD_PARTY_DIR}/${input}")
    message(FATAL_ERROR "${input} is missing; run clients/ps5-native/scripts/fetch-deps.sh")
  endif()
endforeach()

set(AKN_WARNINGS -Wall -Wextra -Wshadow -Wformat=2 -Wstrict-prototypes -Wvla -Wundef -Wpointer-arith)
if(AKN_WERROR)
  list(APPEND AKN_WARNINGS -Werror)
endif()
set(AKN_HARDENING -fstack-protector-strong -D_FORTIFY_SOURCE=2)
if(AKN_SANITIZE)
  # float-cast-overflow is not part of GCC's "undefined" group (found by tests/fuzz/fuzz_path.c).
  add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined,float-cast-overflow)
  set(AKN_HARDENING -fstack-protector-strong)
endif()

find_package(Threads REQUIRED)

# ---- The existing AKENO device client library (clients/ps5/src), unchanged ----------------------
set(AKENO_CORE_SOURCES buf.c json.c json_write.c crypto_util.c url.c backoff.c caps.c http.c ws.c client.c realtime.c net_posix.c)
list(TRANSFORM AKENO_CORE_SOURCES PREPEND "${PS5_DIR}/src/")
if(EXISTS "${AKENO_THIRD_PARTY_DIR}/mbedtls/library/ssl_tls.c" AND EXISTS "${AKENO_THIRD_PARTY_DIR}/cacert.pem")
  # The console transport, so the PC tests exercise the same TLS code as the PS5 build.
  file(GLOB AKN_MBEDTLS_SOURCES "${AKENO_THIRD_PARTY_DIR}/mbedtls/library/*.c")
  add_library(akn_mbedtls STATIC ${AKN_MBEDTLS_SOURCES})
  target_include_directories(akn_mbedtls SYSTEM PUBLIC "${AKENO_THIRD_PARTY_DIR}/mbedtls/include" "${PS5_DIR}/config")
  target_compile_definitions(akn_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="akeno_mbedtls_config.h")
  target_link_libraries(akn_mbedtls PUBLIC Threads::Threads)
  add_library(akn_akeno STATIC ${AKENO_CORE_SOURCES} "${PS5_DIR}/src/transport_mbedtls.c")
  target_compile_definitions(akn_akeno PRIVATE
    "AKENO_CA_BUNDLE_PATH=\"${AKENO_THIRD_PARTY_DIR}/cacert.pem\""
    "AKENO_CA_BUNDLE_NAME=\"Mozilla CA bundle from certifi 2026.07.22\"")
  set_source_files_properties("${PS5_DIR}/src/transport_mbedtls.c" PROPERTIES OBJECT_DEPENDS "${AKENO_THIRD_PARTY_DIR}/cacert.pem")
  target_link_libraries(akn_akeno PUBLIC akn_mbedtls)
  set(AKN_TRANSPORT "mbedTLS (console transport)")
else()
  # The updater verifies signatures with mbedTLS (src/core/update_verify.c), as on the console.
  message(FATAL_ERROR "mbedTLS and the CA bundle are missing; run clients/ps5-native/scripts/fetch-deps.sh")
endif()
message(STATUS "AKENO Native: client library transport ${AKN_TRANSPORT}")
target_include_directories(akn_akeno PUBLIC "${PS5_DIR}/include" PRIVATE "${PS5_DIR}/src")
target_compile_options(akn_akeno PRIVATE -Wall -Wextra ${AKN_HARDENING})
target_link_libraries(akn_akeno PUBLIC m Threads::Threads)

# ---- stb_image (pinned by scripts/fetch-deps.sh), JPEG and PNG only: third-party code -------------
add_library(akn_stbimage STATIC src/third/stb_image_impl.c)
target_include_directories(akn_stbimage PRIVATE "${AKN_THIRD_PARTY_DIR}" src/core)
target_compile_options(akn_stbimage PRIVATE -w)

# ---- libopus 1.5.2 (pinned by scripts/fetch-deps.sh): the party voice codec, third-party code ----
# Portable C, float build, no SIMD or runtime CPU detection, no DNN; the same files and defines as
# console/Makefile (cmake/opus_sources.txt is generated from opus' own *_sources.mk lists).
file(STRINGS cmake/opus_sources.txt AKN_OPUS_SOURCES)
list(TRANSFORM AKN_OPUS_SOURCES PREPEND "${AKN_THIRD_PARTY_DIR}/opus/")
add_library(akn_opus STATIC ${AKN_OPUS_SOURCES})
target_include_directories(akn_opus PUBLIC "${AKN_THIRD_PARTY_DIR}/opus/include"
  PRIVATE "${AKN_THIRD_PARTY_DIR}/opus" "${AKN_THIRD_PARTY_DIR}/opus/celt" "${AKN_THIRD_PARTY_DIR}/opus/silk"
          "${AKN_THIRD_PARTY_DIR}/opus/silk/float")
target_compile_definitions(akn_opus PRIVATE OPUS_BUILD VAR_ARRAYS HAVE_LRINT HAVE_LRINTF "PACKAGE_VERSION=\"1.5.2\"")
target_compile_options(akn_opus PRIVATE -w -O2)
target_link_libraries(akn_opus PUBLIC m)

# ---- Portable core (no SDL): unit-tested -------------------------------------------------------
add_library(akn_core STATIC
  src/core/nav.c src/core/input.c src/core/audio_test.c src/core/redact.c src/core/log.c src/core/context.c
  src/core/logo.c src/core/png.c src/core/text.c src/core/ui.c src/core/script.c src/core/account.c src/core/social.c
  src/core/image.c src/core/vector.c src/core/badges.c src/core/artwork.c src/core/chat.c src/core/osk.c src/core/party.c
  src/core/voice.c src/core/voice_codec.c src/core/voice_dsp.c src/core/voice_proto.c src/core/jitter.c src/core/mic_test.c
  src/core/semver.c src/core/update.c src/core/update_verify.c src/core/update_zip.c src/core/update_fs.c
  src/core/update_install.c src/core/update_net.c src/core/update_engine.c
  "${PS5_DIR}/app/audio_lab_pcm.c" "${PS5_DIR}/app/game_detect.c")
target_include_directories(akn_core PUBLIC src/core "${PS5_DIR}/app" "${PS5_DIR}/src")
target_compile_options(akn_core PRIVATE ${AKN_WARNINGS} ${AKN_HARDENING})
target_link_libraries(akn_core PUBLIC akn_akeno akn_stbimage akn_opus m Threads::Threads)

# ---- Desktop app (SDL2) -------------------------------------------------------------------------
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SDL2 IMPORTED_TARGET sdl2>=2.0.18)
endif()
if(SDL2_FOUND)
  execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE AKN_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT AKN_GIT_COMMIT)
    set(AKN_GIT_COMMIT "dev")
  endif()
  add_library(akn_stb STATIC src/app/font_stb.c)
  target_include_directories(akn_stb PUBLIC "${AKN_THIRD_PARTY_DIR}")
  target_link_libraries(akn_stb PUBLIC PkgConfig::SDL2)
  target_compile_options(akn_stb PRIVATE -w) # third-party code

  add_executable(akeno-native src/app/main.c src/app/app.c src/app/screens.c src/app/gfx.c src/app/net.c src/app/report.c
    src/app/ipc.c src/app/social.c src/app/images.c src/app/voice_app.c src/platform/platform_host.c)
  target_include_directories(akeno-native PRIVATE src/app src/platform "${PS5_DIR}/src")
  target_compile_options(akeno-native PRIVATE ${AKN_WARNINGS} ${AKN_HARDENING})
  target_compile_definitions(akeno-native PRIVATE
    "AKN_BUILD_ID=\"${AKN_GIT_COMMIT}-pc\""
    "AKN_FONT_REGULAR_PATH=\"${AKN_THIRD_PARTY_DIR}/inter/Inter-Regular.ttf\""
    "AKN_FONT_BOLD_PATH=\"${AKN_THIRD_PARTY_DIR}/inter/Inter-Bold.ttf\"")
  set_source_files_properties(src/app/app.c PROPERTIES OBJECT_DEPENDS
    "${AKN_THIRD_PARTY_DIR}/inter/Inter-Regular.ttf;${AKN_THIRD_PARTY_DIR}/inter/Inter-Bold.ttf")
  target_link_libraries(akeno-native PRIVATE akn_core akn_stb PkgConfig::SDL2)
else()
  message(WARNING "SDL2 (>= 2.0.18) not found: only the core library, the tools and the unit tests are built")
endif()

# ---- Tools and tests ----------------------------------------------------------------------------
add_executable(make-icon tools/make_icon.c)
target_compile_options(make-icon PRIVATE ${AKN_WARNINGS} ${AKN_HARDENING})
target_link_libraries(make-icon PRIVATE akn_core)

enable_testing()
add_executable(akn_unit_tests tests/unit/test_main.c tests/unit/test_nav_input.c tests/unit/test_audio.c
  tests/unit/test_redact_context.c tests/unit/test_image_text.c tests/unit/test_ui_script.c tests/unit/test_account.c
  tests/unit/test_social.c tests/unit/test_pictures.c tests/unit/test_artwork.c tests/unit/test_chat.c tests/unit/test_party.c tests/unit/test_voice.c tests/unit/test_update.c src/app/net.c src/app/ipc.c)
target_include_directories(akn_unit_tests PRIVATE tests/unit src/app src/platform)
target_compile_options(akn_unit_tests PRIVATE ${AKN_WARNINGS})
target_link_libraries(akn_unit_tests PRIVATE akn_core)
add_test(NAME unit COMMAND akn_unit_tests)
