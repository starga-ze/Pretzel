file(GLOB_RECURSE MGMTD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-mgmtd ${MGMTD_SRC_FILES})

target_include_directories(pz-mgmtd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(pz-mgmtd PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(pz-mgmtd PRIVATE
    pz-shared
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
)
