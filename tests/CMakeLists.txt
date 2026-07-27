# Unit tests. Deliberately NOT part of the default build: `./pretzel build` compiles the
# daemons only, and `./pretzel test` builds this suite and runs it. Keeping the executables out
# of ALL means a product build never pays for compiling them and never fails because of them.
#
#   ./pretzel test                                              # build everything, then run
#   ./pretzel test -R Legacy                                    # ctest args pass through
#   cd build && ctest --output-on-failure                       # re-run without rebuilding
#   ./build/bin/test-password-hash --gtest_filter='*Legacy*'    # one case, verbose
#
# GoogleTest lives in 3rd_party/install/googletest (./pretzel install puts it there). A tree
# without it still builds every daemon — see find_package(GTest QUIET) in the root list file.

# Declared before the GTest guard so `make build-tests` always resolves. Without this, a machine
# lacking GoogleTest would fail with "No rule to make target 'build-tests'" instead of simply
# having nothing to run.
add_custom_target(build-tests)

if(NOT GTest_FOUND)
    message(STATUS "GoogleTest not found — skipping tests/ (run ./pretzel install to enable)")
    return()
endif()

include(GoogleTest)

# Most suites are plain: link pz-shared (which brings shared/ on the include path plus OpenSSL
# and Threads, all PUBLIC on that target) and take main() from gtest_main.
#
# PRE_TEST defers test enumeration to ctest time; the default (POST_BUILD) would need the
# executable built during a plain `make`, which EXCLUDE_FROM_ALL specifically prevents.
function(pz_add_test name source)
    add_executable(${name} EXCLUDE_FROM_ALL ${source})

    target_link_libraries(${name} PRIVATE
        pz-shared
        GTest::gtest
        ${ARGN}
    )

    add_dependencies(build-tests ${name})
    gtest_discover_tests(${name} DISCOVERY_MODE PRE_TEST)
endfunction()

pz_add_test(test-password-hash      test_password_hash.cpp      GTest::gtest_main)
pz_add_test(test-ipc-protocol       test_ipc_protocol.cpp       GTest::gtest_main)
pz_add_test(test-ipc-codec          test_ipc_codec.cpp          GTest::gtest_main)
pz_add_test(test-static-file-cache  test_static_file_cache.cpp  GTest::gtest_main)
pz_add_test(test-url-encode         test_url_encode.cpp         GTest::gtest_main)
pz_add_test(test-api-refs           test_api_refs.cpp           GTest::gtest_main)

# No gtest_main: secret::masterKey() caches the key file in a function-local static, so this
# suite needs its own main() to point PRETZEL_CONFIG_DIR at a fixture before any test runs.
pz_add_test(test-secret             test_secret.cpp)

# buildCollectionSample lives in scand, not shared: pull in just that one translation unit and put
# scand/ on the include path (it needs its own headers and ClientResponse from scand/http). Boost
# and nlohmann come transitively from pz-shared, so no extra links are required.
pz_add_test(test-collection-sample  test_collection_sample.cpp  GTest::gtest_main)
target_sources(test-collection-sample PRIVATE
    ${CMAKE_SOURCE_DIR}/scand/service/api/CollectionSample.cpp)
target_include_directories(test-collection-sample PRIVATE ${CMAKE_SOURCE_DIR}/scand)
