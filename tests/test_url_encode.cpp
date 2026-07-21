// Covers pz::http::urlEncode.
//
// Small, but it sits on the credential path: the PAN-OS keygen call passes the operator's
// username and password as query parameters, so a character this fails to escape either breaks
// the login or truncates the credential silently. RFC 3986 unreserved set is the contract.

#include "http/HttpClient.h"

#include <gtest/gtest.h>

#include <string>

using pz::http::urlEncode;

TEST(UrlEncode, LeavesUnreservedCharactersAlone)
{
    // RFC 3986: ALPHA / DIGIT / "-" / "." / "_" / "~"
    EXPECT_EQ("abcXYZ", urlEncode("abcXYZ"));
    EXPECT_EQ("0123456789", urlEncode("0123456789"));
    EXPECT_EQ("-._~", urlEncode("-._~"));
}

TEST(UrlEncode, EncodesSpaceAsPercentTwenty)
{
    // Not '+': that form is only correct in application/x-www-form-urlencoded bodies, and this
    // builds query strings.
    EXPECT_EQ("%20", urlEncode(" "));
    EXPECT_EQ("a%20b", urlEncode("a b"));
}

TEST(UrlEncode, EncodesTheDelimitersThatWouldBreakAQueryString)
{
    // A password containing any of these would otherwise be read as structure, not data.
    EXPECT_EQ("%26", urlEncode("&"));
    EXPECT_EQ("%3D", urlEncode("="));
    EXPECT_EQ("%3F", urlEncode("?"));
    EXPECT_EQ("%23", urlEncode("#"));
    EXPECT_EQ("%2F", urlEncode("/"));
    EXPECT_EQ("%2B", urlEncode("+"));
    EXPECT_EQ("%25", urlEncode("%"));
}

TEST(UrlEncode, EncodesXmlSyntaxUsedByThePanosCommandApi)
{
    // cmd=<show><system><info/></system></show> is typed raw by the operator.
    EXPECT_EQ("%3Cshow%3E", urlEncode("<show>"));
    EXPECT_EQ("%3C%2Fshow%3E", urlEncode("</show>"));
}

TEST(UrlEncode, UsesUppercaseHexDigits)
{
    // RFC 3986 says producers should emit uppercase; some devices compare percent-escapes
    // literally when validating a signature.
    EXPECT_EQ("%3F", urlEncode("?"));
    EXPECT_EQ("%2F", urlEncode("/"));
    EXPECT_EQ("%FF", urlEncode("\xff"));
}

TEST(UrlEncode, EncodesEachByteOfMultibyteUtf8)
{
    // "한" is E1 95 9C in UTF-8 — three separate escapes, not one.
    EXPECT_EQ("%ED%95%9C", urlEncode("\xed\x95\x9c"));
}

TEST(UrlEncode, EncodesControlCharacters)
{
    EXPECT_EQ("%0A", urlEncode("\n"));
    EXPECT_EQ("%0D", urlEncode("\r"));
    EXPECT_EQ("%09", urlEncode("\t"));
}

TEST(UrlEncode, EncodesAnEmbeddedNulRatherThanTruncating)
{
    // std::string may carry a NUL; treating it as a terminator would silently shorten a
    // credential and produce a confusing authentication failure.
    const std::string withNul("a\0b", 3);

    EXPECT_EQ("a%00b", urlEncode(withNul));
}

TEST(UrlEncode, ReturnsEmptyForEmptyInput)
{
    EXPECT_EQ("", urlEncode(""));
}

TEST(UrlEncode, HandlesARealisticPassword)
{
    EXPECT_EQ("p%40ss%20w%2Frd%21%23%24", urlEncode("p@ss w/rd!#$"));
}

TEST(UrlEncode, IsNotIdempotent)
{
    // Encoding twice double-escapes; this pins the fact so nobody "fixes" a bug by calling it
    // again somewhere up the stack.
    EXPECT_EQ("%2520", urlEncode(urlEncode(" ")));
}
