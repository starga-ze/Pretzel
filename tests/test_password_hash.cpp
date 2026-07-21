// Covers pz::util::PasswordHash — local-account credential storage.
//
// This is authentication code with no second chance: a mistake here either locks every operator
// out (legacy hashes stop verifying) or silently weakens storage (the KDF is not doing what it
// claims). Both are invisible until they matter, so the properties are pinned here rather than
// left to inspection.

#include "util/PasswordHash.h"

#include <iostream>
#include <string>

namespace
{

int failures = 0;

void check(const std::string& name, bool ok)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << name << "\n";
    if (!ok)
        ++failures;
}

}

int main()
{
    using namespace pz::util;

    // ── The KDF is really PBKDF2-HMAC-SHA256 ────────────────────────────────────────────
    // Published vectors for P="password", S="salt", dkLen=32. These pin the algorithm itself:
    // if the implementation were quietly swapped or misconfigured, these stop matching.
    check("pbkdf2 vector c=1",
          verifyPassword("password", "salt",
                         "pbkdf2$1$120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    check("pbkdf2 vector c=4096",
          verifyPassword("password", "salt",
                         "pbkdf2$4096$c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    check("pbkdf2 vector rejects a near miss",
          !verifyPassword("Password", "salt",
                          "pbkdf2$1$120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));

    // A value is verified at the cost stored inside it, not the current one — otherwise raising
    // kPbkdf2Iterations would lock out every existing account at once.
    check("stored cost is honoured, not the current one",
          verifyPassword("password", "salt",
                         "pbkdf2$4096$c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));

    // ── Upgrade path from the pre-PBKDF2 format ─────────────────────────────────────────
    // sha256("password" + "salt"), which is exactly what earlier builds wrote. If this check
    // ever fails, the deployed operators cannot log in.
    check("legacy sha256 hash still verifies",
          verifyPassword("password", "salt",
                         "7a37b85c8918eac19a9089c0fa5a2ab4dce3f90528dcdeec108b23ddf3607b99"));
    check("legacy hash rejects a wrong password",
          !verifyPassword("wrong", "salt", "7a37b85c8918eac19a9089c0fa5a2ab4dce3f90528dcdeec108b23ddf3607b99"));
    check("legacy hash is flagged for rehash",
          needsRehash("7a37b85c8918eac19a9089c0fa5a2ab4dce3f90528dcdeec108b23ddf3607b99"));
    check("a hash below the current cost is flagged for rehash", needsRehash("pbkdf2$1000$aabb"));

    // ── Salt ────────────────────────────────────────────────────────────────────────────
    const std::string salt = generateSalt();
    check("salt is 16 bytes hex-encoded", salt.size() == 32);
    check("salt is not degenerate", salt != std::string(32, '0'));
    check("salts differ between calls", generateSalt() != generateSalt());

    // ── Round trip ──────────────────────────────────────────────────────────────────────
    const std::string stored = hashPassword("hunter2", salt);
    check("hash is self-describing", stored.rfind("pbkdf2$", 0) == 0);
    check("correct password verifies", verifyPassword("hunter2", salt, stored));
    check("wrong password is rejected", !verifyPassword("hunter3", salt, stored));
    check("right password with the wrong salt is rejected", !verifyPassword("hunter2", generateSalt(), stored));
    check("a freshly written hash needs no rehash", !needsRehash(stored));

    // ── Malformed input is refused, never accepted by accident ──────────────────────────
    check("empty stored value is rejected", !verifyPassword("x", salt, ""));
    check("empty password does not match a real hash", !verifyPassword("", salt, stored));
    check("truncated format is rejected", !verifyPassword("x", salt, "pbkdf2$$"));
    check("non-numeric cost is rejected", !verifyPassword("x", salt, "pbkdf2$abc$aabb"));
    check("non-numeric cost is flagged for rehash", needsRehash("pbkdf2$abc$aabb"));

    if (failures)
    {
        std::cout << "\n" << failures << " check(s) failed\n";
        return 1;
    }

    std::cout << "\nall checks passed\n";
    return 0;
}
