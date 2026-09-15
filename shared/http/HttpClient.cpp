#include "http/HttpClient.h"

#include "http/HttpClientSession.h"

#include <exception>
#include <memory>
#include <utility>

namespace pz::http
{

void requestAsync(boost::asio::io_context& ioc, ClientRequest req, ResponseHandler onDone)
{
    try
    {
        // onDone is copied rather than moved: HttpClientSession::run() cannot throw, so reaching the
        // catch means the session was never constructed and never took ownership of the handler.
        std::make_shared<HttpClientSession>(ioc, std::move(req), onDone)->run();
    }
    catch (const std::exception& e)
    {
        ClientResponse out;
        out.error = e.what();
        if (onDone)
            onDone(std::move(out));
    }
}

ClientResponse requestSync(ClientRequest req)
{
    boost::asio::io_context ioc;

    ClientResponse out;
    out.error = "request never settled";

    requestAsync(ioc, std::move(req), [&out](ClientResponse res) { out = std::move(res); });

    // run() returns once the session has finished and released itself; the deadlines inside bound
    // how long that can take, so there is no separate guard here.
    ioc.run();

    return out;
}

}
