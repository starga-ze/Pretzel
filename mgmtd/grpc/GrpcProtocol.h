#pragma once

#include <cstdint>

namespace pz::mgmtd
{

// The pretzel-ai RPC surface, named the way IpcCmd names the IPC one.
//
// mgmtd talks to exactly one gRPC peer, so there is no addressing here and no wire format to
// version — this enum exists for the reason IpcCmd does: so a call is dispatched by naming what it
// is, rather than by the router growing one method per operation. A router with a method per
// operation is a router that has to be edited every time the service gains a call, and that is the
// point at which it starts holding opinions about them.
enum class GrpcCmd : std::uint16_t
{
    Unknown = 0,

    // ── The assistant ──
    Chat = 1,

    // ── The tech-doc knowledge base ──
    CorpusStatus = 3,    // what the store holds now
    CorpusRefresh = 4,   // apply it; streams progress for minutes
    // Not a call — it cancels the streaming one already in flight. Routed as a command anyway so
    // the transport keeps its single entry point: a controller that reached past the router to
    // cancel would be the first thing to know a transport detail.
    CorpusCancel = 5,
    CorpusDocuments = 6,   // the documents under one product/book, for the corpus browser
};

const char* grpcCmdToStr(GrpcCmd cmd) noexcept;

// True for calls that report many times before they finish. The distinction is what decides how a
// completion is filed: a unary call resolves a ticket once and is drained by whoever polls it, a
// streaming one overwrites a single live slot that any number of polls can read.
bool grpcCmdStreams(GrpcCmd cmd) noexcept;

}
