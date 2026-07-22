#include "client/HttpClient.h"

#include "client/HttpClientSession.h"

#include <exception>
#include <memory>
#include <utility>

namespace pz::http
{

void requestAsync(boost::asio::io_context& ioc, ClientRequest req, ResponseHandler onDone)
{
    try
    {
        // onDone is copied rather than moved: ClientSession::run() cannot throw, so reaching the
        // catch means the session was never constructed and never took ownership of the handler.
        std::make_shared<ClientSession>(ioc, std::move(req), onDone)->run();
    }
    catch (const std::exception& e)
    {
        ClientResponse out;
        out.error = e.what();
        if (onDone)
            onDone(std::move(out));
    }
}

}
