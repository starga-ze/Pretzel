# Minimal test harness: plain executables registered with ctest, no external framework.
#
# Deliberately dependency-free — pretzel vendors its libraries under 3rd_party/, and pulling in
# GoogleTest to assert a hash matches a published vector would cost more than it returns. If the
# suite grows past what a check() helper can express, swap this for a real framework then.
#
# Run with:  cd build && ctest --output-on-failure

add_executable(test-password-hash test_password_hash.cpp)

target_link_libraries(test-password-hash PRIVATE
    pz-shared
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_include_directories(test-password-hash PRIVATE ${CMAKE_SOURCE_DIR}/shared)

add_test(NAME password-hash COMMAND test-password-hash)
