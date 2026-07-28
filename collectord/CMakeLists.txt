file(GLOB_RECURSE COLLECTORD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-collectord ${COLLECTORD_SRC_FILES})

target_include_directories(pz-collectord PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Vendor HTTPS: collectord runs the periodic collection, so it pulls in pz::http's outbound client
# (in shared) — hence OpenSSL and Boost.Asio. SNMP/net-snmp is still out.
target_link_libraries(pz-collectord PRIVATE
    pz-shared
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
)
