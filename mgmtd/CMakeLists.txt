# gRPC + its bundled protobuf, for the mgmtd <-> pretzel-ai inference transport. REQUIRED now,
# not QUIET: the ChatController path talks to pretzel-ai over gRPC, so the daemon genuinely
# depends on it. Vendored by script/install.py install_grpc() into 3rd_party/install/grpc (added
# to CMAKE_PREFIX_PATH at the top level).
find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Generate the C++ stubs from the mirrored proto into the build tree.
set(PA_PROTO ${CMAKE_CURRENT_SOURCE_DIR}/proto/inference.proto)
set(PA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/pa_gen)
file(MAKE_DIRECTORY ${PA_GEN_DIR})

set(PA_PB_CC   ${PA_GEN_DIR}/inference.pb.cc)
set(PA_PB_H    ${PA_GEN_DIR}/inference.pb.h)
set(PA_GRPC_CC ${PA_GEN_DIR}/inference.grpc.pb.cc)
set(PA_GRPC_H  ${PA_GEN_DIR}/inference.grpc.pb.h)

add_custom_command(
    OUTPUT ${PA_PB_CC} ${PA_PB_H} ${PA_GRPC_CC} ${PA_GRPC_H}
    COMMAND $<TARGET_FILE:protobuf::protoc>
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
            --cpp_out=${PA_GEN_DIR}
            --grpc_out=${PA_GEN_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
            ${PA_PROTO}
    DEPENDS ${PA_PROTO} protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "protoc: generating pretzel-ai gRPC C++ stubs"
    VERBATIM
)

# The daemon's own sources, including the gRPC client (grpc/GrpcClient.cpp) and its handler.
file(GLOB_RECURSE MGMTD_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(pz-mgmtd ${MGMTD_SRC_FILES} ${PA_PB_CC} ${PA_GRPC_CC})

target_include_directories(pz-mgmtd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PA_GEN_DIR}
)

target_link_libraries(pz-mgmtd PRIVATE
    pz-shared
    OpenSSL::SSL
    OpenSSL::Crypto
    Boost::boost
    Threads::Threads
    gRPC::grpc++
    protobuf::libprotobuf
)
