cmake_minimum_required(VERSION 3.16)
project(akeno_device_client VERSION 0.1.0 LANGUAGES C)

# Host build (Linux) of the AKENO NETWORK device client. A console build needs a different
# transport implementation (see docs/INSTALLATION.md); none is enabled here.

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
  src/buf.c src/json.c src/crypto_util.c src/url.c src/backoff.c src/caps.c src/http.c src/ws.c)
target_include_directories(akeno_core PUBLIC include PRIVATE src)
target_compile_options(akeno_core PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno_core PUBLIC m)

# Client with the POSIX + OpenSSL transport.
add_library(akeno_client STATIC src/client.c src/realtime.c src/httpclient_posix.c src/transport_openssl.c)
target_include_directories(akeno_client PUBLIC include PRIVATE src)
target_compile_options(akeno_client PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno_client PUBLIC akeno_core OpenSSL::SSL OpenSSL::Crypto)

add_executable(akeno-cli cli/main.c)
target_compile_options(akeno-cli PRIVATE ${AKENO_WARNINGS} ${AKENO_HARDENING})
target_link_libraries(akeno-cli PRIVATE akeno_client)

enable_testing()
add_executable(akeno_unit_tests tests/unit/test_main.c tests/unit/test_core.c)
target_include_directories(akeno_unit_tests PRIVATE src)
target_compile_options(akeno_unit_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(akeno_unit_tests PRIVATE akeno_client)
add_test(NAME unit COMMAND akeno_unit_tests)
