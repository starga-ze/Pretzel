#include "service/advisory/AdvisoryService.h"

#include "event/CollectordEventFactory.h"
#include "service/CollectordServiceManager.h"

#include "config/Config.h"
#include "http/HttpClient.h"
#include "ipc/IpcMessage.h"
#include "ipc/IpcProtocol.h"
#include "util/Logger.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <nlohmann/json.hpp>

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace pz::collectord
{

using json = nlohmann::json;

// Internal linkage for this file's constants and its stateless helpers. Not decoration: `lower`,
// `stringArray` and `numberOf` are names another translation unit is entitled to use for its own
// helper — mgmtd's advisory reader already has a `stringArray` — and without an unnamed namespace
// the two would be one symbol at link time.
namespace
{

// Defaults, used whole when `pretzel.advisory` is absent — which it is until someone configures
// it. Written as defaults rather than as a required config section so this works on an appliance
// that has not been reconfigured since the feature shipped; the section only has to exist to
// change one of them.
constexpr const char* kDefaultHost = "security.paloaltonetworks.com";

// Six hours. The feed publishes a handful of advisories a week, and the alerting half of this
// feature is what wants minutes — that will poll the 8 KB RSS document with an ETag, not this. A
// full walk of the catalogue is the slow, complete pass underneath it.
constexpr int kDefaultIntervalSec = 6 * 60 * 60;

// 100 is the largest page that answers reliably; the endpoint starts returning HTTP 500 somewhere
// between 300 and 320, and a page of 100 is already ~700 KB of JSON.
constexpr int kDefaultPageSize = 100;

// Twelve pages of 100 is 1,200 advisories against the 588 the feed holds today. A ceiling, not an
// expectation: without one a feed that always answered a full page would walk for ever.
constexpr int kDefaultMaxPages = 12;

// Between pages. The same 0.2s the tech-doc crawler uses for 22,000 requests, doubled — there are
// only a dozen requests here and no reason to hurry them.
constexpr std::chrono::milliseconds kPageThrottle{400};

// Far enough after start that the daemon is up and the first config read has settled, soon enough
// that a fresh appliance shows advisories while the operator is still looking at the page.
constexpr std::chrono::seconds kInitialDelay{15};

// A frame is capped at 1 MiB. Flushing on serialised size rather than on a record count is what
// keeps that safe whatever the vendor puts in a record — one advisory with a long workaround can
// be several times the size of an ordinary one.
constexpr std::size_t kBatchFlushBytes = 600 * 1024;

// Names this product to the far end, with the feature after it, so an operator on the other side
// reading their access log can tell what is calling and why. Same shape as the tech-doc crawler's.
constexpr const char* kUserAgent = "pz-pretzel/1.0 (+security-advisory)";

std::string sha256Hex(const std::string& in)
{
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int len = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1
        || EVP_DigestUpdate(ctx, in.data(), in.size()) != 1
        || EVP_DigestFinal_ex(ctx, digest, &len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i)
        out << std::setw(2) << static_cast<int>(digest[i]);
    return out.str();
}

// ASCII lower-case. A plain loop rather than std::transform with a lambda: the cast to unsigned
// char is what makes it correct for bytes above 0x7F, and it is easier to see here than inside a
// callable passed as the fourth argument of an algorithm.
std::string lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The vendor writes these in caps and has shipped advisories with no severity at all. Anything
// outside the five bands becomes `none`, which is also what engined's CHECK constraint accepts —
// a new word in the feed must not take a whole batch down with it.
std::string normalizeSeverity(const json& a)
{
    for (const char* key : {"severity", "baseSeverity", "threatSeverity"})
    {
        if (!a.contains(key) || !a[key].is_string())
            continue;
        const std::string s = lower(a[key].get<std::string>());
        if (s == "critical" || s == "high" || s == "medium" || s == "low")
            return s;
    }
    return "none";
}

// A JSON value that may arrive as a number or as a string of one — the feed has done both for
// baseScore. Anything else yields nothing, which engined stores as NULL.
bool numberOf(const json& a, const char* key, double& out)
{
    if (!a.contains(key))
        return false;
    if (a[key].is_number())
    {
        out = a[key].get<double>();
        return true;
    }
    if (a[key].is_string())
    {
        try
        {
            out = std::stod(a[key].get<std::string>());
            return true;
        }
        catch (const std::exception&)
        {
        }
    }
    return false;
}

std::vector<std::string> stringArray(const json& a, const char* key)
{
    std::vector<std::string> out;
    if (!a.contains(key))
        return out;
    if (a[key].is_string())
    {
        out.push_back(a[key].get<std::string>());
        return out;
    }
    if (!a[key].is_array())
        return out;
    for (const auto& v : a[key])
        if (v.is_string())
            out.push_back(v.get<std::string>());
    return out;
}

// One feed entry, reshaped into the row engined stores.
//
// The hash deliberately excludes the two timestamps. `updated` is also the feed's RSS sort key and
// moves for reasons that are not content; hashing it would mark an advisory revised every time the
// vendor touched the ordering, and "revised" is the signal this whole page is built around. What is
// left is the advisory itself, so a revision means the text, the score, the affected range or the
// fix actually changed.
json toRow(const json& a)
{
    json row;
    row["id"] = a.value("ID", std::string());
    row["title"] = a.value("title", std::string());
    row["severity"] = normalizeSeverity(a);

    double score = 0;
    if (numberOf(a, "baseScore", score))
        row["base_score"] = score;
    if (numberOf(a, "threatScore", score))
        row["threat_score"] = score;

    row["published_at"] = a.value("date", std::string());
    row["source_updated"] = a.value("updated", std::string());

    // Deduplicated: the feed repeats a product once per affected version line.
    auto products = stringArray(a, "product");
    std::sort(products.begin(), products.end());
    products.erase(std::unique(products.begin(), products.end()), products.end());
    row["products"] = products;

    // "None" is how the feed spells "no fixed release for this line", so a fix exists only when
    // some line names one.
    bool hasFix = false;
    for (const auto& f : stringArray(a, "fixed"))
        if (!f.empty() && f != "None")
            hasFix = true;
    row["has_fix"] = hasFix;

    row["record"] = a;

    json forHash = a;
    forHash.erase("date");
    forHash.erase("updated");
    row["content_sha"] = sha256Hex(forHash.dump());

    return row;
}

std::string nowIso()
{
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

}

// Everything one walk of the feed needs, alive for as long as the walk is. `sm` lives for the
// process (owned by the core), so the raw pointer is safe for the run's lifetime.
struct AdvisoryRun
{
    CollectordServiceManager* sm{nullptr};
    boost::asio::steady_timer timer;

    std::string host{kDefaultHost};
    int pageSize{kDefaultPageSize};
    int maxPages{kDefaultMaxPages};
    std::chrono::seconds interval{kDefaultIntervalSec};
    bool enabled{true};

    // True from the moment a walk starts until its final batch is sent. A run-now that arrives
    // while this is set is dropped: the operator wants current data and some is on its way.
    bool running{false};
    int page{1};
    int listed{0};
    std::string startedAt;
    json batch = json::array();
    // Tracked as rows are appended rather than measured by serialising the batch each time. The
    // obvious `batch.dump().size()` after every push is quadratic — 190 rows means 190 dumps of a
    // growing 600 KB document — and this is an estimate of exactly the thing the flush cares about.
    std::size_t batchBytes{0};

    explicit AdvisoryRun(boost::asio::io_context& ioc) : timer(ioc) {}
};

AdvisoryService::AdvisoryService(CollectordEventFactory* eventFactory) : m_eventFactory(eventFactory)
{
}

// Out of line, and it has to be: m_run is a unique owner of AdvisoryRun, which is incomplete in the
// header. The destructor is where that type has to be complete.
AdvisoryService::~AdvisoryService() = default;

std::unique_ptr<CollectordEvent> AdvisoryService::schedule(std::chrono::steady_clock::time_point now)
{
    (void)now;

    if (m_setupDone)
        return nullptr;

    m_setupDone = true;
    return m_eventFactory->create(CollectordEventDomain::Advisory,
                                  static_cast<std::uint32_t>(AdvisoryEventType::Setup));
}

void AdvisoryService::handleEvent(CollectordServiceManager& serviceManager, const AdvisoryEvent& event)
{
    switch (event.type())
    {
    case AdvisoryEventType::Setup:
        // Nothing to wait for: the feed is public, so unlike the connector collector in ApiService
        // this has no issued key to race against.
        start(serviceManager);
        break;

    case AdvisoryEventType::RunRefresh:
        runNow();
        break;

    default:
        LOG_WARN("unhandled event (type={})", static_cast<std::uint32_t>(event.type()));
        break;
    }
}

void AdvisoryService::start(CollectordServiceManager& sm)
{
    m_run = std::make_unique<AdvisoryRun>(sm.ioContext());
    m_run->sm = &sm;

    const auto& cfg = pz::config::Config::section(pz::config::scope::kPretzel, "advisory");
    m_run->enabled = cfg.value("enabled", true);
    m_run->host = cfg.value("host", std::string(kDefaultHost));
    m_run->pageSize = std::max(1, std::min(cfg.value("page_size", kDefaultPageSize), 300));
    m_run->maxPages = std::max(1, cfg.value("max_pages", kDefaultMaxPages));
    m_run->interval = std::chrono::seconds(std::max(300, cfg.value("poll_interval_sec", kDefaultIntervalSec)));

    if (!m_run->enabled)
    {
        LOG_INFO("advisory collection disabled by config");
        return;
    }

    LOG_INFO("advisory collection armed (host={}, interval={}s)", m_run->host, m_run->interval.count());
    armRun(std::chrono::duration_cast<std::chrono::milliseconds>(kInitialDelay));
}

void AdvisoryService::runNow()
{
    if (!m_run || !m_run->enabled)
    {
        LOG_WARN("advisory refresh ignored — collection is not armed");
        return;
    }
    if (m_run->running)
    {
        LOG_INFO("advisory refresh ignored — a collection is already in flight");
        return;
    }
    LOG_INFO("advisory refresh requested — collecting now");
    armRun(std::chrono::milliseconds(0));
}

// The four stages below call one another in a cycle — arm → fetch → onPage → finish → arm. As
// members they need no forward declarations: a member function may call any other member whatever
// order they are defined in, which is the whole reason this is a class and not a file of free
// functions threading a shared_ptr through itself.
//
// The handlers capture `this`. The service owns the timer, so destroying it cancels the wait, and
// an outstanding HTTP completion is destroyed with the io_context rather than invoked — neither
// can reach a dead `this`.

void AdvisoryService::armRun(std::chrono::milliseconds delay)
{
    m_run->timer.expires_after(delay);
    m_run->timer.async_wait(
        [this](const boost::system::error_code& ec)
        {
            if (ec)   // cancelled — the daemon is shutting down
                return;

            if (m_run->running)
                return fetchPage();   // mid-walk: this was the page throttle

            if (!m_run->enabled)
                return;

            m_run->running = true;
            m_run->page = 1;
            m_run->listed = 0;
            m_run->batch = json::array();
            m_run->batchBytes = 0;
            m_run->startedAt = nowIso();
            LOG_INFO("advisory collection started (host={}, page_size={})", m_run->host, m_run->pageSize);
            fetchPage();
        });
}

void AdvisoryService::fetchPage()
{
    pz::http::ClientRequest req;
    req.host = m_run->host;
    req.port = 443;
    req.method = "GET";
    req.target = "/json?limit=" + std::to_string(m_run->pageSize) + "&page=" + std::to_string(m_run->page);
    req.headers.emplace_back("User-Agent", kUserAgent);
    req.headers.emplace_back("Accept", "application/json");
    // A public CA-signed host, not a managed device: verify the chain and the hostname. The
    // fingerprint pinning next door exists for PAN-OS boxes whose certificate names a chassis
    // serial, and pinning a vendor's public endpoint would break on their next certificate.
    req.verifyCa = true;
    req.timeout = std::chrono::seconds(20);

    pz::http::requestAsync(m_run->sm->ioContext(), std::move(req),
                           [this](pz::http::ClientResponse res) { onPage(res); });
}

void AdvisoryService::onPage(const pz::http::ClientResponse& res)
{
    if (!res.error.empty() || res.status != 200)
    {
        // A run that dies halfway leaves every advisory already written standing — the upsert never
        // deletes — so the console keeps the previous catalogue and the sync row says the last
        // attempt failed. That is the honest state, and it is why there is no "empty the table
        // first" step anywhere in this path.
        //
        // A redirect is named rather than reported as a bare status, because it is the one failure
        // here with an obvious cause and no obvious symptom. The shared client does not follow
        // redirects and does not surface headers, so there is no Location to chase and nothing in
        // "HTTP 301" says the feed simply moved. Teaching the client to follow them is the real
        // fix, and the thing that would also unlock the retired-id alias map (see Database.cpp).
        std::string why;
        if (!res.error.empty())
            why = res.error;
        else if (res.status >= 300 && res.status < 400)
            why = "feed redirected (HTTP " + std::to_string(res.status)
                  + "); the HTTP client does not follow redirects — check the feed path";
        else
            why = "HTTP " + std::to_string(res.status);
        return finishRun("failed", why);
    }

    json page;
    try
    {
        page = json::parse(res.body);
    }
    catch (const std::exception& e)
    {
        return finishRun("failed", std::string("unparseable feed page: ") + e.what());
    }

    if (!page.is_array())
        return finishRun("failed", "feed page is not an array");

    const int received = static_cast<int>(page.size());
    for (const auto& a : page)
    {
        if (!a.is_object() || !a.contains("ID"))
            continue;
        json row = toRow(a);
        m_run->batchBytes += row.dump().size();
        m_run->batch.push_back(std::move(row));
        ++m_run->listed;

        // Flushed on serialised size: the frame limit is a byte count, so the batch boundary has
        // to be one too. The running total ignores the array's own commas and the envelope around
        // it, which is why the threshold sits well under the 1 MiB frame rather than near it.
        if (m_run->batchBytes >= kBatchFlushBytes)
            sendBatch(false, {}, {});
    }

    // A short page is the end of the feed. The ceiling is the other way out, for a feed that would
    // otherwise never give one.
    if (received < m_run->pageSize || m_run->page >= m_run->maxPages)
        return finishRun("ok", {});

    ++m_run->page;
    armRun(kPageThrottle);
}

void AdvisoryService::finishRun(const std::string& status, const std::string& error)
{
    sendBatch(true, status, error);
    m_run->running = false;

    if (status == "ok")
        LOG_INFO("advisory collection finished (listed={}, pages={})", m_run->listed, m_run->page - 1);
    else
        LOG_WARN("advisory collection failed (listed_so_far={}, error={})", m_run->listed, error);

    armRun(std::chrono::duration_cast<std::chrono::milliseconds>(m_run->interval));
}

// Hands one batch to engined. `final` carries the run's outcome with it rather than in a message of
// its own, so a run can never be recorded as finished by a frame that arrives without its rows.
void AdvisoryService::sendBatch(bool final, const std::string& status, const std::string& error)
{
    json payload;
    payload["advisories"] = m_run->batch;
    if (final)
    {
        json meta;
        meta["final"] = true;
        meta["status"] = status;
        meta["listed"] = m_run->listed;
        meta["started_at"] = m_run->startedAt;
        meta["error"] = error;
        payload["run"] = std::move(meta);
    }

    const std::string body = payload.dump();

    auto msg = std::make_unique<pz::ipc::IpcMessage>();
    msg->setSrc(pz::ipc::IpcDaemon::Collectord);
    msg->setDst(pz::ipc::IpcDaemon::Engined);
    msg->setCmd(pz::ipc::IpcCmd::AdvisoryUpdate);
    msg->setFlags(pz::ipc::IpcProtocol::toFlag(pz::ipc::IpcFlag::Request));
    msg->setPayload(std::vector<std::uint8_t>(body.begin(), body.end()));

    m_run->sm->txRouter().handleIpcMessage(std::move(msg));
    m_run->batch = json::array();
    m_run->batchBytes = 0;
}

}
