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
pz_add_test(test-byte-ring-buffer   test_byte_ring_buffer.cpp   GTest::gtest_main)
pz_add_test(test-http-beast        test_http_beast.cpp         GTest::gtest_main)
pz_add_test(test-base64            test_base64.cpp             GTest::gtest_main)
pz_add_test(test-hex               test_hex.cpp                GTest::gtest_main)
pz_add_test(test-ticket-map        test_ticket_map.cpp         GTest::gtest_main)
pz_add_test(test-timestamp         test_timestamp.cpp          GTest::gtest_main)

# No gtest_main: secret::masterKey() caches the key file in a function-local static, so this
# suite needs its own main() to point PRETZEL_CONFIG_DIR at a fixture before any test runs.
pz_add_test(test-secret             test_secret.cpp)

# buildCollectionSample lives in collectord's ApiUtil (with the connector-test helpers), not shared:
# pull in that one translation unit and put collectord/ on the include path (it needs its own headers;
# ClientResponse comes from shared/http). The test only calls buildCollectionSample — a pure function
# of its inputs — so --gc-sections drops the sibling IPC helpers (sendApiCredentialState et al.) and
# their collectord-service link dependencies. Boost/nlohmann come transitively from pz-shared.
pz_add_test(test-collection-sample  test_collection_sample.cpp  GTest::gtest_main)
target_sources(test-collection-sample PRIVATE
    ${CMAKE_SOURCE_DIR}/collectord/service/api/ApiUtil.cpp)
target_include_directories(test-collection-sample PRIVATE ${CMAKE_SOURCE_DIR}/collectord)
target_compile_options(test-collection-sample PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test-collection-sample PRIVATE -Wl,--gc-sections)

# Same arrangement for topologyd's NGFW classifier: one translation unit of pure functions over
# vendor JSON, with topologyd/ on the include path for its own header. No daemon dependencies —
# NgfwModel deliberately touches no database, config or IPC, which is what makes this testable.
pz_add_test(test-ngfw-model         test_ngfw_model.cpp         GTest::gtest_main)
target_sources(test-ngfw-model PRIVATE
    ${CMAKE_SOURCE_DIR}/topologyd/service/topology/NgfwModel.cpp)
target_include_directories(test-ngfw-model PRIVATE ${CMAKE_SOURCE_DIR}/topologyd)

# probed's ICMP wire codec. Three translation units with no socket, no engine and no service
# behind them: the codec turns an IcmpPacket into bytes and back, which is the whole reason the
# framing logic was kept out of IcmpConnection.
pz_add_test(test-icmp-codec         test_icmp_codec.cpp         GTest::gtest_main)
target_sources(test-icmp-codec PRIVATE
    ${CMAKE_SOURCE_DIR}/probed/icmp/IcmpCodec.cpp
    ${CMAKE_SOURCE_DIR}/probed/icmp/IcmpPacket.cpp
    ${CMAKE_SOURCE_DIR}/probed/icmp/IcmpProtocol.cpp)
target_include_directories(test-icmp-codec PRIVATE ${CMAKE_SOURCE_DIR}/probed)

# authd's OIDC client. Only OktaClient.cpp is pulled in — not SamlClient — so the test needs no
# xmlsec and no libxml2. The suite stays on the offline half of the client: the authorize URL it
# builds, and the callback guards that run before the token endpoint is ever called.
pz_add_test(test-okta-client        test_okta_client.cpp)
target_sources(test-okta-client PRIVATE
    ${CMAKE_SOURCE_DIR}/authd/service/auth/OktaClient.cpp)
target_include_directories(test-okta-client PRIVATE ${CMAKE_SOURCE_DIR}/authd)

# authd's SAML client. Unlike the OIDC one this drags in real dependencies — xmlsec for signature
# verification and zlib for the HTTP-Redirect DEFLATE — so the test declares the same packages
# authd itself does. zlib is wanted twice over: SamlClient deflates, and the test inflates the
# result back to XML to check what the IdP would actually receive.
pkg_check_modules(XMLSEC QUIET xmlsec1-openssl)
find_package(ZLIB QUIET)
if(XMLSEC_FOUND AND ZLIB_FOUND)
    pz_add_test(test-saml-client    test_saml_client.cpp)
    target_sources(test-saml-client PRIVATE
        ${CMAKE_SOURCE_DIR}/authd/service/auth/SamlClient.cpp)
    target_include_directories(test-saml-client PRIVATE ${CMAKE_SOURCE_DIR}/authd ${XMLSEC_INCLUDE_DIRS})
    target_link_libraries(test-saml-client PRIVATE ${XMLSEC_LIBRARIES} ZLIB::ZLIB)
    target_compile_options(test-saml-client PRIVATE ${XMLSEC_CFLAGS_OTHER})
else()
    message(STATUS "xmlsec1-openssl or zlib not found — skipping test-saml-client")
endif()

# apid's HTTP routing. Extracted out of IngestService into its own translation unit precisely so it
# could be linked without the service manager, the action queue and the Tx router behind it — the
# routing decision is pure, everything else in that class is plumbing.
pz_add_test(test-ingest-routing     test_ingest_routing.cpp)
target_sources(test-ingest-routing PRIVATE
    ${CMAKE_SOURCE_DIR}/apid/service/ingest/IngestRouting.cpp)
target_include_directories(test-ingest-routing PRIVATE ${CMAKE_SOURCE_DIR}/apid)

# The rest of collectord's ApiUtil — the helpers every device exchange is assembled from (URL
# splitting, OAuth and keygen request building, PAN-OS XML reading, the TLS/pin step record).
# Same link arrangement as test-collection-sample: one translation unit, with --gc-sections
# dropping the IPC senders that would otherwise pull the whole daemon in.
pz_add_test(test-api-util           test_api_util.cpp           GTest::gtest_main)
target_sources(test-api-util PRIVATE
    ${CMAKE_SOURCE_DIR}/collectord/service/api/ApiUtil.cpp)
target_include_directories(test-api-util PRIVATE ${CMAKE_SOURCE_DIR}/collectord)
target_compile_options(test-api-util PRIVATE -ffunction-sections -fdata-sections)
target_link_options(test-api-util PRIVATE -Wl,--gc-sections)

# engined's config→inventory projection. Extracted out of ProbeService so the reading is separable
# from the writing: the SQL stays in the service, and the decision about which rows should exist —
# the one that silently deletes a device when it goes wrong — links with no database behind it.
pz_add_test(test-inventory-projection  test_inventory_projection.cpp  GTest::gtest_main)
target_sources(test-inventory-projection PRIVATE
    ${CMAKE_SOURCE_DIR}/engined/service/probe/InventoryProjection.cpp)
target_include_directories(test-inventory-projection PRIVATE ${CMAKE_SOURCE_DIR}/engined)

# mgmtd's commit gate. Extracted out of SettingsController so the judgement links without an HTTP
# request, a commit queue or a database behind it; ApiRefs comes from pz-shared, and Config::section
# answers with an empty object when nothing is loaded — which is the useful baseline here, since it
# makes the merged view exactly the batch being committed.
pz_add_test(test-settings-validation  test_settings_validation.cpp  GTest::gtest_main)
target_sources(test-settings-validation PRIVATE
    ${CMAKE_SOURCE_DIR}/mgmtd/service/web/controller/SettingsValidation.cpp)
target_include_directories(test-settings-validation PRIVATE ${CMAKE_SOURCE_DIR}/mgmtd)

# mgmtd's web helpers. Header-only, so only the include path is needed — no controller, no service
# manager. parseBody is the reason this exists: it is the one gate every handler's body goes
# through, and the per-controller copies it replaced all missed the same case.
pz_add_test(test-web-util           test_web_util.cpp           GTest::gtest_main)
target_include_directories(test-web-util PRIVATE ${CMAKE_SOURCE_DIR}/mgmtd)
