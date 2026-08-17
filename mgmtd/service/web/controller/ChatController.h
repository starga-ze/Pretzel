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

    // inferd finished a turn. Correlated by seqNo, which is the ticket the browser is polling on.
    void onChatResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);

    // inferd found the passages, ahead of the answer and on the same ticket. Filed separately so
    // the poll can hand them over while the model is still working.
    void onRetrieveResponse(MgmtdServiceManager& sm, const pz::ipc::IpcMessage& msg);
};

}
