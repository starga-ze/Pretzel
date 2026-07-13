file(GLOB_RECURSE APID_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-apid ${APID_SRC_FILES})

target_include_directories(pz-apid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pz-apid PRIVATE
    pz-shared
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
)
