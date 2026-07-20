file(GLOB_RECURSE SCAND_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-scand ${SCAND_SRC_FILES})

target_include_directories(pz-scand PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Collection stack (SNMP/net-snmp + vendor HTTPS/OpenSSL) removed — bare daemon shell for now.
target_link_libraries(pz-scand PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
)
