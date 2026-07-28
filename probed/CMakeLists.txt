file(GLOB_RECURSE PROBED_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-probed ${PROBED_SRC_FILES})

target_include_directories(pz-probed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Vendor HTTPS: probed runs the connector tests and credential auto-refresh, so it pulls in
# pz::http's outbound client (in shared) — hence OpenSSL and Boost.Asio, as collectord does.
target_link_libraries(pz-probed PRIVATE
    pz-shared
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
)
