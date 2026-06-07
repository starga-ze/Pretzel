file(GLOB_RECURSE ICMPD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-icmpd ${ICMPD_SRC_FILES})

target_include_directories(pz-icmpd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(pz-icmpd PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(pz-icmpd PRIVATE
    pz-shared
)
