file(GLOB_RECURSE AUTHD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-authd ${AUTHD_SRC_FILES})

target_include_directories(pz-authd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pz-authd PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
)
