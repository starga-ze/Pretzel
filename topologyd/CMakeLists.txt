file(GLOB_RECURSE TOPOLOGYD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-topologyd ${TOPOLOGYD_SRC_FILES})

target_include_directories(pz-topologyd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pz-topologyd PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
)
