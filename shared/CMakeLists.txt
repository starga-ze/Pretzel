file(GLOB_RECURSE SHARED_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(pz-shared ${SHARED_SRC_FILES})

target_include_directories(pz-shared PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pz-shared PUBLIC
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    PostgreSQL::PostgreSQL
    # shared/http (Beast/Asio HTTP transport) needs Boost headers, OpenSSL for TLS,
    # and Threads for asio. PUBLIC so every daemon that links pz-shared inherits them.
    Boost::boost
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
