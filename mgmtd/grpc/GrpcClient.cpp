#include "grpc/GrpcClient.h"

#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/json_util.h>

#include "pretzel_ai.grpc.pb.h"

#include <chrono>
#include <mutex>

namespace pz::mgmtd
{

namespace v1 = ::pretzel::ai::v1;

struct GrpcClient::Impl
{
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<v1::PretzelAi::Stub> stub;

    // The in-flight refresh's context, so another thread can cancel it. Guarded because the
    // cancelling thread and the streaming thread are different by definition.
    std::mutex refreshMutex;
    grpc::ClientContext* activeRefresh = nullptr;
};

GrpcClient::GrpcClient(const std::string& target)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    m_impl->stub = v1::PretzelAi::NewStub(m_impl->channel);
}

GrpcClient::~GrpcClient() = default;

GrpcClient::Outcome GrpcClient::chat(const std::string& model,
                                              const std::string& message,
                                              const std::string& systemPrompt,
                                              const std::vector<GrpcMessage::Turn>& history,
                                              const std::string& sessionId,
                                              const std::function<void(const std::string&)>& on_delta)
{
    v1::ChatRequest request;
    request.set_model(model);
    request.set_message(message);
    request.set_system_prompt(systemPrompt);
    request.set_session_id(sessionId);

    // Oldest first, and `message` is not among them: the server appends this turn after the ones
    // sent here, so including it would ask the question twice.
    for (const auto& turn : history)
    {
        v1::ChatTurn* out = request.add_history();
        out->set_role(turn.role);
        out->set_content(turn.content);
    }

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

namespace
{

// Proto -> JSON for the console. always_print_primitive_fields matters: a count that has fallen
// back to zero must still appear, or the card would render "—" where the answer is "none".
std::string toJson(const google::protobuf::Message& message)
{
    google::protobuf::util::JsonPrintOptions options;
    options.always_print_primitive_fields = true;
    options.preserve_proto_field_names = true;
    std::string out;
    if (!google::protobuf::util::MessageToJsonString(message, &out, options).ok())
        return "{}";
    return out;
}

}

std::string GrpcClient::corpusStatus(std::string& error)
{
    grpc::ClientContext ctx;
    v1::CorpusStatusRequest request;
    v1::CorpusStatus reply;
    const grpc::Status status = m_impl->stub->GetCorpusStatus(&ctx, request, &reply);
    if (!status.ok())
    {
        error = "gRPC transport error: " + status.error_message();
        return {};
    }
    if (!reply.error().empty())
        error = reply.error();
    return toJson(reply);
}

std::string GrpcClient::corpusDocuments(const std::string& product, const std::string& docset,
                                        std::string& error)
{
    v1::ListDocumentsRequest request;
    request.set_product(product);
    request.set_docset(docset);

    grpc::ClientContext ctx;
    v1::DocumentList reply;
    const grpc::Status status = m_impl->stub->ListDocuments(&ctx, request, &reply);
    if (!status.ok())
    {
        error = "gRPC transport error: " + status.error_message();
        return {};
    }
    if (!reply.error().empty())
        error = reply.error();
    return toJson(reply);
}

void GrpcClient::refreshCorpus(const std::string& scope,
                               const std::function<void(const std::string&)>& on_progress,
                               std::string& error)
{
    v1::RefreshCorpusRequest request;
    request.set_scope(scope);

    grpc::ClientContext ctx;
    {
        std::lock_guard<std::mutex> lock(m_impl->refreshMutex);
        m_impl->activeRefresh = &ctx;
    }
    // Cleared before ctx goes out of scope, whatever path leaves this function.
    struct Clear
    {
        Impl* impl;
        ~Clear()
        {
            std::lock_guard<std::mutex> lock(impl->refreshMutex);
            impl->activeRefresh = nullptr;
        }
    } clear{m_impl.get()};

    std::unique_ptr<grpc::ClientReader<v1::RefreshProgress>> reader(
        m_impl->stub->RefreshCorpus(&ctx, request));

    v1::RefreshProgress progress;
    while (reader->Read(&progress))
    {
        on_progress(toJson(progress));
        if (progress.final() && !progress.error().empty())
            error = progress.error();
    }

    const grpc::Status status = reader->Finish();
    if (!status.ok() && error.empty())
        error = "gRPC transport error: " + status.error_message();
}

void GrpcClient::cancelRefresh()
{
    std::lock_guard<std::mutex> lock(m_impl->refreshMutex);
    if (m_impl->activeRefresh)
        m_impl->activeRefresh->TryCancel();
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
