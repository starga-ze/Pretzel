#pragma once

#include "http/HttpMessage.h"

namespace pz::ipc
{
class IpcMessage;
}

namespace pz::mgmtd
{

class MgmtdServiceManager;

// AI ▸ Assistant, the server half of it.
//
// mgmtd owns no inference logic — it validates the turn, hands it to inferd, and serves whatever
// came back. The reason the daemon exists at all is the same one that keeps this controller thin:
// a turn takes seconds, and responses here are built synchronously on the loop every other daemon's
// IPC arrives on.
//
// So the browser never waits on the socket. POST answers 202 with a ticket, inferd's answer is filed
// under that ticket when it lands, and the page polls for it — the same shape as the connector tests
// in ApiController, for the same reason. It also happens to be the shape streaming will need: a poll
// that returns a growing answer is an additive change, where a held-open POST would have to be
// rebuilt into one.
class ChatController
{
public:
    // POST /api/chat  {model, message} → 202 {ticket, status:"pending"}
    void send(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // GET /api/chat/result?ticket=<n> → {status:"pending"} until inferd answers, then the turn.
    void result(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // POST /api/chat/retrieve {query, k, docset?, version?} → {hits, took_ms, ...}
    //
    // The corpus lookup that precedes a turn, proxied to ragd. It is a separate call rather than a
    // step folded into send() because the operator is meant to SEE what was retrieved and judge it
    // before anything goes upstream — an answer is only as good as the passages behind it, and a
    // retrieval that missed is the failure worth catching early.
    //
    // Unlike send(), this answers inline instead of handing back a ticket. The hop is loopback and
    // costs tens of milliseconds, where a turn costs seconds; the ticket machinery exists for the
    // latter and would only add a round trip here. If retrieval ever grows slow enough to be felt
    // on the shared loop, it moves to the same pattern send() already uses.
    void retrieve(MgmtdServiceManager& sm, const pz::http::HttpRequest& req, pz::http::HttpResponse& resp);

    // inferd finished a turn. Correlated by seqNo, which is the ticket the browser is polling on.
    void onChatResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);

    // inferd found the passages, ahead of the answer and on the same ticket. Filed separately so
    // the poll can hand them over while the model is still working.
    void onRetrieveResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
