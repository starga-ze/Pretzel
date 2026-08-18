#include "grpc/GrpcClient.h"

#include <grpcpp/grpcpp.h>

#include "inference.grpc.pb.h"

#include <chrono>

namespace pz::mgmtd
{

namespace v1 = ::pretzel::ai::v1;

struct GrpcClient::Impl
{
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<v1::Inference::Stub> stub;
};

GrpcClient::GrpcClient(const std::string& target)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    m_impl->stub = v1::Inference::NewStub(m_impl->channel);
}

GrpcClient::~GrpcClient() = default;

GrpcClient::Outcome GrpcClient::chat(const std::string& model,
                                              const std::string& message,
                                              const std::string& systemPrompt,
                                              const std::function<void(const std::string&)>& on_delta)
{
    v1::ChatRequest request;
    request.set_model(model);
    request.set_message(message);
    request.set_system_prompt(systemPrompt);

    grpc::ClientContext ctx;
    std::unique_ptr<grpc::ClientReader<v1::ChatChunk>> reader(
        m_impl->stub->Chat(&ctx, request));

    // The final chunk (done=true) carries the complete turn document and, on a failed turn, the
    // reason. A transport failure (reader->Finish() not ok) is distinct — the stream may never
    // reach that final chunk — and is reported on its own.
    Outcome out;
    v1::ChatChunk chunk;
    while (reader->Read(&chunk))
    {
        if (!chunk.delta().empty())
            on_delta(chunk.delta());
        if (chunk.done())
        {
            if (!chunk.error().empty())
                out.error = chunk.error();
            if (!chunk.result_json().empty())
                out.resultJson = chunk.result_json();
        }
    }

    const grpc::Status status = reader->Finish();
    if (!status.ok())
        out.error = "gRPC transport error: " + status.error_message();

    return out;
}

int GrpcClient::connectivityState(bool tryToConnect)
{
    return static_cast<int>(m_impl->channel->GetState(tryToConnect));
}

bool GrpcClient::waitForStateChange(int lastState, int timeoutMs)
{
    const auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(timeoutMs);
    return m_impl->channel->WaitForStateChange(
        static_cast<grpc_connectivity_state>(lastState), deadline);
}

}
