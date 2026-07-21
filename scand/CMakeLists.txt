file(GLOB_RECURSE SCAND_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-scand ${SCAND_SRC_FILES})

target_include_directories(pz-scand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Vendor HTTPS is back: scand runs the connector tests and (next) the periodic collection, so it
# pulls in pz::http's outbound client — hence OpenSSL and Boost.Asio. SNMP/net-snmp is still out.
target_link_libraries(pz-scand PRIVATE
    pz-shared
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
)
