file(GLOB_RECURSE SCAND_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-scand ${SCAND_SRC_FILES})

target_include_directories(pz-scand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

# api/ uses Boost.Beast (header-only) over OpenSSL for the vendor HTTPS clients.
target_link_libraries(pz-scand PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
    netsnmp
    OpenSSL::SSL
    OpenSSL::Crypto
)
