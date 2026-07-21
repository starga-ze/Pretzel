// Covers pz::http::StaticFileCache's pure helpers — path safety, URL normalisation, content
// type and ETag.
//
// safeTarget() is the one that matters most: it is the only thing standing between a request
// target and the filesystem under /opt/pretzel/share. mgmtd serves the operator UI, so a
// traversal here reads arbitrary files as root.

#include "http/StaticFileCache.h"

#include <gtest/gtest.h>

#include <string>

using pz::http::StaticFileCache;

// ── safeTarget: the traversal gate ──────────────────────────────────────────────────────

TEST(StaticFileSafety, AcceptsOrdinaryAbsolutePaths)
{
    EXPECT_TRUE(StaticFileCache::safeTarget("/"));
    EXPECT_TRUE(StaticFileCache::safeTarget("/index.html"));
    EXPECT_TRUE(StaticFileCache::safeTarget("/js/main.js"));
    EXPECT_TRUE(StaticFileCache::safeTarget("/css/main.css"));
}

TEST(StaticFileSafety, RejectsAnEmptyTarget)
{
    EXPECT_FALSE(StaticFileCache::safeTarget(""));
}

TEST(StaticFileSafety, RejectsARelativeTarget)
{
    // Everything downstream concatenates baseDir + target, so a target that does not start at
    // the root would escape the directory by construction.
    EXPECT_FALSE(StaticFileCache::safeTarget("index.html"));
    EXPECT_FALSE(StaticFileCache::safeTarget("js/main.js"));
}

TEST(StaticFileSafety, RejectsParentDirectoryTraversal)
{
    EXPECT_FALSE(StaticFileCache::safeTarget("/../etc/passwd"));
    EXPECT_FALSE(StaticFileCache::safeTarget("/js/../../etc/shadow"));
    EXPECT_FALSE(StaticFileCache::safeTarget("/.."));
}

TEST(StaticFileSafety, RejectsTraversalHiddenMidPath)
{
    // A prefix check alone would pass this — the whole string has to be examined.
    EXPECT_FALSE(StaticFileCache::safeTarget("/assets/../../../root/.ssh/id_rsa"));
}

TEST(StaticFileSafety, RejectsAnyDoubleDotEvenInAFilename)
{
    // Conservative by design: "a..b.html" is not a real asset name, and allowing dots to appear
    // in pairs anywhere makes the rule harder to reason about than it is worth.
    EXPECT_FALSE(StaticFileCache::safeTarget("/a..b.html"));
}

// ── normalize: extensionless paths become .html ─────────────────────────────────────────

TEST(StaticFileNormalize, RootBecomesIndex)
{
    EXPECT_EQ("/index.html", StaticFileCache::normalize("/"));
}

TEST(StaticFileNormalize, LeavesAPathThatAlreadyHasAnExtension)
{
    EXPECT_EQ("/index.html", StaticFileCache::normalize("/index.html"));
    EXPECT_EQ("/js/main.js", StaticFileCache::normalize("/js/main.js"));
    EXPECT_EQ("/favicon.ico", StaticFileCache::normalize("/favicon.ico"));
}

TEST(StaticFileNormalize, AppendsHtmlToAnExtensionlessLastSegment)
{
    // So /settings serves settings.html — the UI links to bare names.
    EXPECT_EQ("/settings.html", StaticFileCache::normalize("/settings"));
    EXPECT_EQ("/home.html", StaticFileCache::normalize("/home"));
}

TEST(StaticFileNormalize, OnlyTheLastSegmentDecides)
{
    // A dot earlier in the path must not be mistaken for the file's extension.
    EXPECT_EQ("/v1.2/settings.html", StaticFileCache::normalize("/v1.2/settings"));
}

TEST(StaticFileNormalize, IsIdempotent)
{
    const std::string once = StaticFileCache::normalize("/settings");
    EXPECT_EQ(once, StaticFileCache::normalize(once));
}

// ── contentType ─────────────────────────────────────────────────────────────────────────

TEST(StaticFileContentType, MapsTheAssetTypesTheUiShips)
{
    EXPECT_EQ("text/html; charset=utf-8", StaticFileCache::contentType("/index.html"));
    EXPECT_EQ("application/javascript; charset=utf-8", StaticFileCache::contentType("/js/main.js"));
    EXPECT_EQ("text/css; charset=utf-8", StaticFileCache::contentType("/css/main.css"));
    EXPECT_EQ("application/json; charset=utf-8", StaticFileCache::contentType("/data.json"));
    EXPECT_EQ("image/svg+xml", StaticFileCache::contentType("/icon.svg"));
    EXPECT_EQ("image/png", StaticFileCache::contentType("/logo.png"));
    EXPECT_EQ("image/x-icon", StaticFileCache::contentType("/favicon.ico"));
}

TEST(StaticFileContentType, FallsBackToPlainTextForAnythingElse)
{
    // Falling back to text/plain keeps an unknown asset from being executed by the browser as
    // something it is not.
    EXPECT_EQ("text/plain; charset=utf-8", StaticFileCache::contentType("/README"));
    EXPECT_EQ("text/plain; charset=utf-8", StaticFileCache::contentType("/archive.tar.gz"));
    EXPECT_EQ("text/plain; charset=utf-8", StaticFileCache::contentType(""));
}

TEST(StaticFileContentType, MatchesOnTheSuffixNotASubstring)
{
    // "/js.html" is html, not javascript.
    EXPECT_EQ("text/html; charset=utf-8", StaticFileCache::contentType("/js.html"));
    EXPECT_EQ("text/plain; charset=utf-8", StaticFileCache::contentType("/main.js.bak"));
}

TEST(StaticFileContentType, TextTypesCarryACharset)
{
    // Without charset=utf-8 the browser guesses, and the UI has non-ASCII strings in it.
    for (const char* target : {"/a.html", "/a.js", "/a.css", "/a.json"})
    {
        EXPECT_NE(std::string::npos, StaticFileCache::contentType(target).find("charset=utf-8"))
            << "missing charset for " << target;
    }
}

// ── makeEtag ────────────────────────────────────────────────────────────────────────────

TEST(StaticFileEtag, IsStableForTheSameBody)
{
    const std::string body = "console.log('hi');";
    EXPECT_EQ(StaticFileCache::makeEtag(body), StaticFileCache::makeEtag(body));
}

TEST(StaticFileEtag, ChangesWhenTheBodyChanges)
{
    EXPECT_NE(StaticFileCache::makeEtag("a"), StaticFileCache::makeEtag("b"));
    EXPECT_NE(StaticFileCache::makeEtag("console.log(1)"), StaticFileCache::makeEtag("console.log(2)"));
}

TEST(StaticFileEtag, DistinguishesBodiesOfDifferentLength)
{
    // Length is mixed into the hash specifically so that a truncated file does not keep the
    // etag of the whole one and get served from a stale cache.
    EXPECT_NE(StaticFileCache::makeEtag("abc"), StaticFileCache::makeEtag("abcabc"));

    // The length must be given explicitly: std::string("\0") built from a literal stops at the
    // NUL and is simply empty, so the interesting case would never be exercised.
    EXPECT_NE(StaticFileCache::makeEtag(""), StaticFileCache::makeEtag(std::string("\0", 1)));
    EXPECT_NE(StaticFileCache::makeEtag(std::string("a\0", 2)), StaticFileCache::makeEtag("a"));
}

TEST(StaticFileEtag, IsQuotedAsHttpRequires)
{
    // An unquoted ETag is not a valid header value and clients may ignore it.
    const std::string etag = StaticFileCache::makeEtag("body");

    ASSERT_GE(etag.size(), 2u);
    EXPECT_EQ('"', etag.front());
    EXPECT_EQ('"', etag.back());
}

TEST(StaticFileEtag, HandlesAnEmptyBody)
{
    const std::string etag = StaticFileCache::makeEtag("");

    EXPECT_FALSE(etag.empty());
    EXPECT_EQ('"', etag.front());
}

TEST(StaticFileEtag, IsOrderSensitive)
{
    // A hash that ignored order would collide across files built from the same bytes.
    EXPECT_NE(StaticFileCache::makeEtag("ab"), StaticFileCache::makeEtag("ba"));
}
