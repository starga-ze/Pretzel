file(GLOB_RECURSE AUTHD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-authd ${AUTHD_SRC_FILES})

# SAML: libxml2 + xmlsec (OpenSSL backend) for XML-DSig verification; zlib for the
# HTTP-Redirect AuthnRequest DEFLATE encoding.
find_package(PkgConfig REQUIRED)
pkg_check_modules(XMLSEC REQUIRED xmlsec1-openssl)
find_package(ZLIB REQUIRED)

target_include_directories(pz-authd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${XMLSEC_INCLUDE_DIRS}
)

# io/HttpsClient uses Boost.Beast (header-only) over OpenSSL; service/auth/OktaClient
# uses OpenSSL directly for RS256/JWKS id_token verification; service/auth/SamlClient
# uses xmlsec for SAMLResponse signature verification.
target_link_libraries(pz-authd PRIVATE
    pz-shared
    spdlog::spdlog
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${XMLSEC_LIBRARIES}
    ZLIB::ZLIB
)

target_compile_options(pz-authd PRIVATE ${XMLSEC_CFLAGS_OTHER})
