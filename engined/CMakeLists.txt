file(GLOB_RECURSE ENGINED_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-engined ${ENGINED_SRC_FILES})

target_include_directories(pz-engined PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(pz-engined PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(pz-engined PRIVATE
    pz-shared
)
