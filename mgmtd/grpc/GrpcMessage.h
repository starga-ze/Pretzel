#pragma once

#include "grpc/GrpcProtocol.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

    // CorpusDocuments only: which book's documents to list.
    std::string docset;

    // Chat: one earlier turn of the same conversation, as (role, content).
    struct Turn
    {
        std::string role;
        std::string content;
    };

    // Chat: the turns before this one, oldest first, excluding `message`. Carried rather than
    // remembered — mgmtd holds no conversation of its own; the browser owns the thread and sends
    // what the model should see.
    std::vector<Turn> history;

    // Chat: the conversation id, forwarded all the way to Prisma AIRS as the scan's tr_id so a
    // thread reads as one session there instead of one session per turn.
    std::string sessionId;

    static GrpcMessage chat(std::uint32_t ticket, std::string model, std::string message,
                           std::string systemPrompt, std::vector<Turn> history = {},
                           std::string sessionId = {})
    {
        GrpcMessage out;
        out.cmd = GrpcCmd::Chat;
        out.ticket = ticket;
        out.model = std::move(model);
        out.message = std::move(message);
        out.systemPrompt = std::move(systemPrompt);
        out.history = std::move(history);
        out.sessionId = std::move(sessionId);
        return out;
    }

    static GrpcMessage corpus(GrpcCmd cmd, std::uint32_t ticket, std::string scope = {},
                              std::string docset = {})
    {
        GrpcMessage out;
        out.cmd = cmd;
        out.ticket = ticket;
        out.message = std::move(scope);
        out.docset = std::move(docset);
        return out;
    }
};

}
