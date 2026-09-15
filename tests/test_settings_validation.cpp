// The gate in front of running_config.
//
// A commit that gets past these functions is not a transient error: running_config is
// append-versioned and revertable, so a malformed entry mints a permanent configuration version,
// appears verbatim in every later review diff, is exported by Save-to-file, and is reloaded by
// eight daemons. Several of the rules here are the only thing standing between an operator's
// mistake and an appliance nobody can sign in to, or a secret written into a document that is
// shown to every reviewer.
//
// Extracted out of SettingsController so the judgement can be checked without an HTTP request, a
// commit queue or a database. validateCommitShape is pure; validateCommitRefs reads the stored
// configuration, which is an empty section here — the useful baseline, since it makes the merged
// view exactly the batch being committed.

#include "service/web/controller/SettingsValidation.h"

#include "config/Config.h"

#include <gtest/gtest.h>

#include <string>

using namespace pz::mgmtd::settings;
using json = nlohmann::json;

namespace
{

const std::string kPretzel = pz::config::scope::kPretzel;
const std::string kPretzelAi = pz::config::scope::kPretzelAi;

// Shape check, discarding the message.
bool shapeOk(const std::string& domain, const json& values, const std::string& scope = pz::config::scope::kPretzel)
{
    std::string error;
    return validateCommitShape(scope, domain, values, error);
}

std::string shapeError(const std::string& domain, const json& values,
                       const std::string& scope = pz::config::scope::kPretzel)
{
    std::string error;
    validateCommitShape(scope, domain, values, error);
    return error;
}

json device(std::string oid = "dev-1", std::string target = "10.0.0.1")
{
    return json{{"oid", std::move(oid)}, {"name", "fw"}, {"target", std::move(target)}};
}

json apiKey()
{
    return json{{"oid", "key-1"}, {"name", "sherpain-fw key"}, {"device", "dev-1"}};
}

json ngfwEndpoint()
{
    return json{{"oid", "ep-1"}, {"name", "interfaces"}, {"path", "/restapi/v10.2/Network/EthernetInterfaces"}};
}

json saseEndpoint()
{
    return json{{"oid", "ep-2"},        {"name", "egress"}, {"path", "/getPrismaAccessIP/v2"},
                {"device_type", "sase"}, {"subtype", "ztna"}, {"host", "api.sase.paloaltonetworks.com"}};
}

json connector()
{
    return json{{"oid", "c-1"},
                {"object", "dev-1"},
                {"auth_profile", "key-1"},
                {"items", json::array({json{{"endpoint", "ep-1"}}})}};
}

json user(std::string oid, std::string name, std::string role = "admin")
{
    return json{{"oid", std::move(oid)}, {"username", std::move(name)}, {"role", std::move(role)}};
}

}

// ── The generic contract ────────────────────────────────────────────────────────────────────────

TEST(SettingsShape, AnUnknownScopeOrDomainPassesThrough)
{
    // The commit endpoint is generic on purpose; only the domains the UI publishes carry a schema.
    EXPECT_TRUE(shapeOk("whatever", json{{"anything", 1}}));
    EXPECT_TRUE(shapeOk("site", json::object(), "some-other-scope"));
    EXPECT_TRUE(shapeOk("logger", json{{"dir", "/var/log/pretzel"}}));
}

TEST(SettingsShape, ADomainMayOmitAnArrayItDoesNotTouch)
{
    // Each editor publishes its own slice: api-keys.js sends only api_credentials. A domain schema
    // must not demand the arrays this particular commit is not changing.
    EXPECT_TRUE(shapeOk("connector", json{{"api_credentials", json::array({apiKey()})}}));
    EXPECT_TRUE(shapeOk("site", json{{"sites", json::array()}}));
    EXPECT_TRUE(shapeOk("site", json::object()));
}

TEST(SettingsShape, AnArrayKeyCarryingSomethingElseIsRejectedByName)
{
    EXPECT_FALSE(shapeOk("site", json{{"ngfw_devices", json::object()}}));
    EXPECT_EQ("ngfw_devices must be an array", shapeError("site", json{{"ngfw_devices", "not an array"}}));
}

TEST(SettingsShape, TheErrorNamesTheOffendingEntryByIndexAndName)
{
    // The operator is looking at a list; "entry #2" plus the name is what lets them find it.
    const json values{{"sites", json::array({json{{"oid", "s1"}, {"name", "ok"}},
                                             json{{"oid", "s2"}, {"name", "also ok"}},
                                             json{{"oid", ""}, {"name", "the bad one"}}})}};

    EXPECT_EQ(R"(invalid sites entry #2 ("the bad one"))", shapeError("site", values));
}

TEST(SettingsShape, AnUnnamedBadEntryStillReportsItsIndex)
{
    const json values{{"sites", json::array({json{{"oid", ""}}})}};
    EXPECT_EQ("invalid sites entry #0", shapeError("site", values));
}

// ── pretzel.site ────────────────────────────────────────────────────────────────────────────────

TEST(SettingsSite, ASiteNeedsAnIdentityAndAName)
{
    EXPECT_TRUE(shapeOk("site", json{{"sites", json::array({json{{"oid", "s1"}, {"name", "HQ"}}})}}));

    EXPECT_FALSE(shapeOk("site", json{{"sites", json::array({json{{"name", "HQ"}}})}})) << "no oid";
    EXPECT_FALSE(shapeOk("site", json{{"sites", json::array({json{{"oid", "s1"}}})}})) << "no name";
    EXPECT_FALSE(shapeOk("site", json{{"sites", json::array({"a string"})}})) << "not an object";
}

TEST(SettingsSite, ADeviceNeedsAnIdentityAndSomethingToReachItAt)
{
    EXPECT_TRUE(shapeOk("site", json{{"ngfw_devices", json::array({device()})}}));

    // A device with no target cannot be probed, tested or collected from — it is a row that does
    // nothing, and the operator finds out only when nothing ever happens.
    EXPECT_FALSE(shapeOk("site", json{{"ngfw_devices", json::array({json{{"oid", "d"}, {"name", "fw"}}})}}));
    EXPECT_FALSE(shapeOk("site", json{{"ngfw_devices", json::array({json{{"name", "fw"}, {"target", "1.1.1.1"}}})}}));
}

TEST(SettingsSite, BothDeviceArraysAreHeldToTheSameRule)
{
    // The array is the type since the schema split; the identity+target requirement is shared.
    EXPECT_TRUE(shapeOk("site", json{{"sase_devices", json::array({device("t-1", "1963594622")})}}));
    EXPECT_FALSE(shapeOk("site", json{{"sase_devices", json::array({json{{"oid", "t-1"}}})}}));
}

// ── pretzel.connector — API keys ────────────────────────────────────────────────────────────────

TEST(SettingsApiKey, NeedsAnIdentityANameAndTheDeviceItWasIssuedBy)
{
    EXPECT_TRUE(shapeOk("connector", json{{"api_credentials", json::array({apiKey()})}}));

    for (const char* missing : {"oid", "name", "device"})
    {
        json k = apiKey();
        k.erase(missing);
        EXPECT_FALSE(shapeOk("connector", json{{"api_credentials", json::array({k})}})) << "without " << missing;
    }
}

TEST(SettingsApiKey, RefusesACommitCarryingASecret)
{
    // The rule this whole file exists for. running_config is append-versioned and shown verbatim in
    // the review diff, so a secret committed here would be permanent and readable by every reviewer.
    for (const char* secret : {"password", "secret", "api_key", "key"})
    {
        json k = apiKey();
        k[secret] = "hunter2";
        EXPECT_FALSE(shapeOk("connector", json{{"api_credentials", json::array({k})}})) << secret;
    }
}

TEST(SettingsApiKey, RefusesASecretEvenWhenItIsEmpty)
{
    // The field being present at all means the browser is still sending it; an empty one today is
    // a populated one after the next edit.
    json k = apiKey();
    k["password"] = "";
    EXPECT_FALSE(shapeOk("connector", json{{"api_credentials", json::array({k})}}));
}

TEST(SettingsApiKey, AllowsTheAccountNameWhichIsNotASecret)
{
    json k = apiKey();
    k["username"] = "admin";
    EXPECT_TRUE(shapeOk("connector", json{{"api_credentials", json::array({k})}}));
}

// ── pretzel.connector — endpoints ───────────────────────────────────────────────────────────────

TEST(SettingsEndpoint, NeedsAnIdentityANameAndAnAbsolutePath)
{
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({ngfwEndpoint()})}}));

    for (const char* path : {"", "restapi/v10.2/x", "api/?type=op"})
    {
        json e = ngfwEndpoint();
        e["path"] = path;
        EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << "path '" << path << "'";
    }
}

TEST(SettingsEndpoint, ParametersMustBeNamedPairs)
{
    json e = ngfwEndpoint();

    e["params"] = json::array({json{{"name", "type"}, {"value", "op"}}});
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}}));

    // A parameter with no name cannot be sent; accepting it leaves an endpoint that calls something
    // other than what the page shows.
    e["params"] = json::array({json{{"value", "op"}}});
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));

    e["params"] = json::object();
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsEndpoint, AnAbsentDeviceTypeMeansNgfw)
{
    // Every endpoint written before SASE support existed has no device_type, and must stay valid.
    json e = ngfwEndpoint();
    EXPECT_FALSE(e.contains("device_type"));
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsEndpoint, RejectsADeviceTypeThatIsNeitherKind)
{
    json e = ngfwEndpoint();
    e["device_type"] = "router";
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsEndpoint, AcceptsBothPanOsApiFlavoursAndRejectsOthers)
{
    json e = ngfwEndpoint();
    for (const char* subtype : {"xml", "rest", ""})
    {
        e["subtype"] = subtype;
        EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << "subtype '" << subtype << "'";
    }

    e["subtype"] = "graphql";
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsEndpoint, StillReadsTheOlderApiTypeSpelling)
{
    // subtype replaced api_type/product; an endpoint committed before the merge must stay valid.
    json e = ngfwEndpoint();
    e["api_type"] = "rest";
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}}));

    e["api_type"] = "graphql";
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsSaseEndpoint, NeedsAHostBecauseTheDeviceIsOnlyATenant)
{
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({saseEndpoint()})}}));

    json e = saseEndpoint();
    e.erase("host");
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << "no host";

    // A host, not a URL — the path is its own field, and a slash here would produce a double path.
    e = saseEndpoint();
    e["host"] = "api.sase.paloaltonetworks.com/v2";
    EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

TEST(SettingsSaseEndpoint, ServesOnlyTheProductsThatAreActuallyImplemented)
{
    json e = saseEndpoint();
    for (const char* subtype : {"ztna", "scm"})
    {
        e["subtype"] = subtype;
        EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << subtype;
    }

    // Refused rather than accepted-and-ignored, so a commit cannot leave an endpoint that looks
    // configured and never collects.
    for (const char* subtype : {"swg", "casb", ""})
    {
        e["subtype"] = subtype;
        EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << subtype;
    }
}

TEST(SettingsSaseEndpoint, RefusesAnAuthorizationHeaderInAnyCasing)
{
    // The bearer token is minted per call. A configured one would be a secret in running_config and
    // a stale token in every later request.
    for (const char* name : {"Authorization", "authorization", "AUTHORIZATION", "AuThOrIzAtIoN"})
    {
        json e = saseEndpoint();
        e["headers"] = json::array({json{{"name", name}, {"value", "Bearer abc"}}});
        EXPECT_FALSE(shapeOk("connector", json{{"endpoints", json::array({e})}})) << name;
    }
}

TEST(SettingsSaseEndpoint, AllowsOtherHeaders)
{
    json e = saseEndpoint();
    e["headers"] = json::array({json{{"name", "Accept"}, {"value", "application/json"}}});
    EXPECT_TRUE(shapeOk("connector", json{{"endpoints", json::array({e})}}));
}

// ── pretzel.connector — connectors ──────────────────────────────────────────────────────────────

TEST(SettingsConnector, NeedsAnObjectACredentialAndASchedule)
{
    EXPECT_TRUE(shapeOk("connector", json{{"connectors", json::array({connector()})}}));

    for (const char* missing : {"oid", "object", "auth_profile", "items"})
    {
        json c = connector();
        c.erase(missing);
        EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}})) << "without " << missing;
    }
}

TEST(SettingsConnector, RejectsTheLegacyInlineShapeRatherThanIgnoringIt)
{
    // Before connectors became schedules they carried a single endpoint inline. Silently accepting
    // that shape would leave a connector that collects nothing while looking configured.
    for (const char* legacy : {"endpoint", "params", "poll_interval_sec"})
    {
        json c = connector();
        c[legacy] = "whatever";
        EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}})) << legacy;
    }
}

TEST(SettingsConnector, EveryScheduledItemMustNameAnEndpoint)
{
    json c = connector();
    c["items"] = json::array({json{{"enabled", true}}});
    EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}}));

    c["items"] = json::array({"a string"});
    EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}}));

    c["items"] = json::object();
    EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}}));
}

TEST(SettingsConnector, APollIntervalMustBeAWholeNumberOfSecondsAndAtLeastOne)
{
    json c = connector();

    c["items"] = json::array({json{{"endpoint", "ep-1"}, {"poll_interval_sec", 300}}});
    EXPECT_TRUE(shapeOk("connector", json{{"connectors", json::array({c})}}));

    // Zero or negative would be a schedule that either spins or never runs; a float is a type the
    // scheduler cannot honour.
    for (const json bad : {json(0), json(-1), json(1.5), json("300"), json(true)})
    {
        c["items"] = json::array({json{{"endpoint", "ep-1"}, {"poll_interval_sec", bad}}});
        EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}})) << bad.dump();
    }
}

TEST(SettingsConnector, AnItemsEnabledFlagMustBeABoolean)
{
    json c = connector();

    c["items"] = json::array({json{{"endpoint", "ep-1"}, {"enabled", false}}});
    EXPECT_TRUE(shapeOk("connector", json{{"connectors", json::array({c})}}));

    // "false" as a string is truthy everywhere it would later be read.
    c["items"] = json::array({json{{"endpoint", "ep-1"}, {"enabled", "false"}}});
    EXPECT_FALSE(shapeOk("connector", json{{"connectors", json::array({c})}}));
}

TEST(SettingsConnector, AnEmptyScheduleIsAllowed)
{
    // A connector with nothing enabled yet is a normal intermediate state while the operator builds
    // one up; it collects nothing, which is exactly what it says.
    json c = connector();
    c["items"] = json::array();
    EXPECT_TRUE(shapeOk("connector", json{{"connectors", json::array({c})}}));
}

// ── pretzel.user ────────────────────────────────────────────────────────────────────────────────

TEST(SettingsUsers, AcceptsAWellFormedAccountList)
{
    const json values{{"list", json::array({user("u1", "admin"), user("u2", "jinho", "user")})}};
    EXPECT_TRUE(shapeOk("user", values));
}

TEST(SettingsUsers, RefusesToEmptyTheAccountList)
{
    // The appliance would have no way in. The console disables the button; this is the rule behind
    // the courtesy.
    const std::string error = shapeError("user", json{{"list", json::array()}});
    EXPECT_NE(std::string::npos, error.find("no way in")) << error;
}

TEST(SettingsUsers, RefusesAListWithNoAdminLeft)
{
    const json values{{"list", json::array({user("u1", "jinho", "user")})}};
    const std::string error = shapeError("user", values);
    EXPECT_NE(std::string::npos, error.find("admin")) << error;
}

TEST(SettingsUsers, RequiresAKnownRole)
{
    for (const char* role : {"", "root", "Admin", "superuser"})
    {
        const json values{{"list", json::array({user("u1", "admin"), user("u2", "x", role)})}};
        EXPECT_FALSE(shapeOk("user", values)) << "role '" << role << "'";
    }
}

TEST(SettingsUsers, RestrictsUsernamesToWhatALoginFormAndADatabaseKeyBothCarry)
{
    for (const char* name : {"jinho", "j.kim", "j-kim", "j_kim", "user123"})
    {
        const json values{{"list", json::array({user("u1", "admin"), user("u2", name, "user")})}};
        EXPECT_TRUE(shapeOk("user", values)) << name;
    }

    // Refused rather than trimmed: an account whose name is not what the operator typed is one they
    // cannot sign in as.
    for (const char* name : {"", "jin ho", "jinho@example.com", "jin/ho", "jinho\n", "제이"})
    {
        const json values{{"list", json::array({user("u1", "admin"), user("u2", name, "user")})}};
        EXPECT_FALSE(shapeOk("user", values)) << "name '" << name << "'";
    }
}

TEST(SettingsUsers, RefusesAUsernameLongerThanTheColumnHolds)
{
    const json ok{{"list", json::array({user("u1", "admin"), user("u2", std::string(64, 'a'), "user")})}};
    EXPECT_TRUE(shapeOk("user", ok));

    const json tooLong{{"list", json::array({user("u1", "admin"), user("u2", std::string(65, 'a'), "user")})}};
    EXPECT_FALSE(shapeOk("user", tooLong));
}

TEST(SettingsUsers, RefusesTwoAccountsAnOperatorCannotTellApart)
{
    // Case-insensitively: one of the two is an account somebody signs in to by mistake.
    const json values{{"list", json::array({user("u1", "Admin"), user("u2", "admin", "user")})}};
    const std::string error = shapeError("user", values);
    EXPECT_NE(std::string::npos, error.find("repeats the username")) << error;
}

TEST(SettingsUsers, RefusesARepeatedOid)
{
    const json values{{"list", json::array({user("same", "admin"), user("same", "jinho", "user")})}};
    EXPECT_NE(std::string::npos, shapeError("user", values).find("repeats an oid"));
}

TEST(SettingsUsers, RefusesACommitCarryingAPassword)
{
    json u = user("u1", "admin");
    u["password"] = "hunter2";
    EXPECT_FALSE(shapeOk("user", json{{"list", json::array({u})}}));

    // And at the domain level, not only per entry.
    EXPECT_FALSE(shapeOk("user", json{{"password", "hunter2"},
                                      {"list", json::array({user("u1", "admin")})}}));
}

TEST(SettingsUsers, ADomainCommitWithoutAListChangesNoAccounts)
{
    EXPECT_TRUE(shapeOk("user", json::object()));
    EXPECT_FALSE(shapeOk("user", json{{"list", json::object()}})) << "a list that is not an array";
}

// ── Reference integrity ─────────────────────────────────────────────────────────────────────────

TEST(SettingsRefs, OnlyTheConnectorDomainIsCheckedForReferences)
{
    std::string error;
    EXPECT_TRUE(validateCommitRefs(kPretzel, "site", json{{"ngfw_devices", json::array({device()})}}, error));
    EXPECT_TRUE(validateCommitRefs(kPretzel, "user", json::object(), error));
    EXPECT_TRUE(validateCommitRefs(kPretzelAi, "providers", json::object(), error));
    EXPECT_TRUE(error.empty());
}

TEST(SettingsRefs, AcceptsABatchWhoseReferencesResolveWithinItself)
{
    // The commonest edit there is: add an endpoint and attach it in the same action. Checking the
    // connector against the stored configuration alone would reject it, because the endpoint it
    // points at is in the sibling entry rather than the database yet.
    const json values{{"api_credentials", json::array({apiKey()})},
                      {"endpoints", json::array({ngfwEndpoint()})},
                      {"connectors", json::array({connector()})}};

    std::string error;
    EXPECT_TRUE(validateCommitRefs(kPretzel, "connector", values, error)) << error;
}

TEST(SettingsRefs, RejectsAConnectorPointingAtAnEndpointThatIsNotThere)
{
    json c = connector();
    c["items"] = json::array({json{{"endpoint", "ep-that-was-deleted"}}});

    const json values{{"api_credentials", json::array({apiKey()})},
                      {"endpoints", json::array({ngfwEndpoint()})},
                      {"connectors", json::array({c})}};

    std::string error;
    EXPECT_FALSE(validateCommitRefs(kPretzel, "connector", values, error));
    EXPECT_FALSE(error.empty()) << "the operator is normally deleting something and needs to know what holds it";
}

// ── The scope and domain tables ─────────────────────────────────────────────────────────────────

TEST(SettingsScopes, OnlyOperatorOwnedScopesAreEditable)
{
    // `global` is infrastructure — the IPC socket, where logs go. A console that offered to edit it
    // would be offering to break the fabric it answers on.
    bool sawGlobal = false;
    for (const auto* s : kSettingsScopes)
        sawGlobal = sawGlobal || (std::string(s) == "global");

    EXPECT_FALSE(sawGlobal);
    EXPECT_EQ(2u, std::size(kSettingsScopes));
    EXPECT_EQ(kPretzel, std::string(kSettingsScopes[0]));
    EXPECT_EQ(kPretzelAi, std::string(kSettingsScopes[1]));
}

TEST(SettingsScopes, TheHiddenDomainsAreTheOnesServedElsewhere)
{
    // `bootstrap` is compiled-in tuning; `auth` has its own endpoint, which knows which of its
    // fields may be shown at all.
    EXPECT_EQ(2u, std::size(kHiddenDomains));
    EXPECT_EQ("bootstrap", std::string(kHiddenDomains[0]));
    EXPECT_EQ("auth", std::string(kHiddenDomains[1]));
}
