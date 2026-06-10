file(GLOB_RECURSE SNMPD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-snmpd ${SNMPD_SRC_FILES})

target_include_directories(pz-snmpd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pz-snmpd PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
    netsnmp
)
