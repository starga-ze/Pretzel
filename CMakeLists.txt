cmake_minimum_required(VERSION 3.16)
project(pretzel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(THIRD_PARTY_INSTALL_DIR "${CMAKE_SOURCE_DIR}/3rd_party/install")

set(OPENSSL_ROOT_DIR "${THIRD_PARTY_INSTALL_DIR}/openssl")
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib64/libcrypto.a")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib64/libssl.a")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")

list(APPEND CMAKE_PREFIX_PATH ${THIRD_PARTY_INSTALL_DIR}/spdlog)
list(APPEND CMAKE_PREFIX_PATH ${THIRD_PARTY_INSTALL_DIR}/openssl)
list(APPEND CMAKE_PREFIX_PATH ${THIRD_PARTY_INSTALL_DIR}/boost)
list(APPEND CMAKE_PREFIX_PATH ${THIRD_PARTY_INSTALL_DIR}/nlohmann_json)

find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)
find_package(nlohmann_json REQUIRED)

# libpq (PostgreSQL client) — installed system-wide via apt (libpq-dev). Backs the
# DB-stored running-config / state snapshots / device inventory in shared/db.
find_package(PostgreSQL REQUIRED)

add_compile_options(
    -Wall
    -Wextra
    -fdiagnostics-color=always
)

# Keep symbols that come from static archives — notably our bundled OpenSSL — out of each
# executable's dynamic symbol table.
#
# We link OpenSSL 3.2.0 statically, while the apt libpq is a shared library bound to the
# system OpenSSL (3.0.x). Both end up in the same process. Exported, our static symbols would
# interpose on libpq's copy, because the executable leads the global symbol lookup scope: at
# connect time libssl.so.3 would resolve its internal crypto calls into our 3.2.0 instead of
# its own 3.0.x and segfault inside OPENSSL_init_ssl().
#
# This only bites a daemon that links PART of OpenSSL — libcrypto.a without libssl.a — since
# one linking all of it stays self-consistent by accident. Today no daemon does, so nothing is
# visibly broken; the flag is here so that the next one to call an EVP_*/RAND_* function does
# not resurrect the crash. Marking the archive symbols local keeps our calls on our static copy
# and leaves libpq with the system one.
add_link_options(-Wl,--exclude-libs,ALL)

add_subdirectory(shared)

add_subdirectory(ipcd)
add_subdirectory(engined)
add_subdirectory(icmpd)
add_subdirectory(scand)
add_subdirectory(topologyd)
add_subdirectory(authd)
add_subdirectory(mgmtd)
add_subdirectory(apid)

enable_testing()
add_subdirectory(tests)
