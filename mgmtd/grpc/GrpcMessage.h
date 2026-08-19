#pragma once

#include "grpc/GrpcProtocol.h"

#include <cstdint>
#include <string>

namespace pz::mgmtd

{

// One outbound call to pretzel-ai.
//
// Unlike IpcMessage this carries no header and no serialized payload: nothing here crosses a
// socket in this form. It is the in-process envelope that lets MgmtdTxRouter forward a call
// without knowing which call it is — the fields are named rather than opaque because there are
// two shapes, not twenty, and a byte buffer would cost a serialize/parse pair to hide nothing.
struct GrpcMessage
{
    GrpcCmd cmd{GrpcCmd::Unknown};

    // The ticket the browser polls. Unused by streaming calls, which report into a live slot
    // instead of resolving a ticket.
    std::uint32_t ticket{0};

    // Chat.
    std::string model;
    std::string systemPrompt;

    // Chat: the person's turn. Corpus: the product scope, empty for the whole sitemap.
    std::string message;

    static GrpcMessage chat(std::uint32_t ticket, std::string model, std::string message,
                           std::string systemPrompt)
    {
        GrpcMessage out;
        out.cmd = GrpcCmd::Chat;
        out.ticket = ticket;
        out.model = std::move(model);
        out.message = std::move(message);
        out.systemPrompt = std::move(systemPrompt);
        return out;
    }

    static GrpcMessage corpus(GrpcCmd cmd, std::uint32_t ticket, std::string scope = {})
    {
        GrpcMessage out;
        out.cmd = cmd;
        out.ticket = ticket;
        out.message = std::move(scope);
        return out;
    }
};

}
