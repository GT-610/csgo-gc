#include "stdafx.h"
#include "panorama.h"
#include "config.h"
#include "platform.h"

#include <cstring>

// Relaxing the code.pbin signature check
//
// The Panorama frontend package (csgo/panorama/code.pbin) is a ZIP prefixed with
// a "PAN" magic and a 512-byte RSA-4096 signature, using PKCS#1 v1.5 over SHA-1.
// Valve's panzip tool builds it, and the frontend refuses to load a container
// whose signature does not verify. That signature cannot be reproduced without
// Valve's private key, so a repacked frontend is rejected and the UI never starts,
// which blocks exercising Matchmaking UI against this GC.
//
// What the game does (panorama_s1wrapper/wrap_resource.cpp, PreloadResources):
//
//     if ( !(*g_pfnPanoramaResourceFileIntegrityCheck)( buf, data, len ) )
//         Error( "Resource %s has invalid data.\n", PANORAMA_ZIPFILE_NAME );
//
// The structural check and the signature check are one call, and its result gates
// loading. Making that single conditional branch unconditional accepts the payload
// regardless of signature while the loader keeps parsing normally.
//
// Location strategy
// -----------------
// No address, file offset or instruction encoding is hardcoded. Builds differ per
// platform and the 32-bit and 64-bit ABIs pass arguments very differently (x86
// uses "push imm32", x86-64 uses RIP-relative "lea"), so any anchor tied to the
// argument setup fails on one of them.
//
// The one thing every build must have is a reference to the literal
// "Resource %s has invalid data." in the code that reports it. That literal is
// referenced from exactly one place, and the branch deciding whether that place
// runs is the check we need. So:
//
// 1. Find the literal by content.
// 2. Find the code sites that encode its address, in whichever form the build
//    uses (absolute 32-bit immediate, or RIP-relative displacement).
// 3. Require exactly one such site.
// 4. Walk back to the nearest preceding conditional branch and make it
//    unconditional, so the error site is never taken.
//
// Any step that is absent or ambiguous means we do not understand this build and
// nothing is patched.

namespace Panorama
{

namespace
{

constexpr const char *kModule = "panorama";
constexpr const char *kInvalidDataError = "Resource %s has invalid data.\n";

// The guard sits immediately before the error block in the reference build; this
// window only has to cover a few argument-setup instructions.
constexpr size_t kBranchSearchWindow = 64;

struct ModuleRange
{
    uint8_t *start{ nullptr };
    uint8_t *end{ nullptr };

    size_t Size() const { return static_cast<size_t>(end - start); }
};

// Find a NUL-terminated string by content.
uint8_t *FindString(const ModuleRange &range, const char *value)
{
    const size_t length = strlen(value) + 1;
    if (length > range.Size())
    {
        return nullptr;
    }

    const uint8_t *needle = reinterpret_cast<const uint8_t *>(value);
    uint8_t *limit = range.end - length;

    for (uint8_t *p = range.start; p <= limit; p++)
    {
        if (memcmp(p, needle, length) == 0)
        {
            return p;
        }
    }

    return nullptr;
}

// A code site whose bytes contain a reference to the target string.
struct ReferenceSite
{
    uint8_t *immediate{ nullptr }; // where the address bytes start
    bool ripRelative{ false };
    uint8_t *instructionEnd{ nullptr }; // only meaningful for rip-relative refs
};

// Collect every place in [start, end) that encodes 'target'.
//
// Both supported forms are checked for every offset, which is cheap because the
// scan is a single pass over the executable section:
//   * a literal 32-bit address (what x86 "push $addr" / "mov $addr" encode)
//   * a RIP-relative displacement, resolved against each plausible instruction
//     end. The x86-64 encoding puts the displacement immediately before its
//     trailing immediate, if any, so we try the two common lengths.
std::vector<ReferenceSite> FindReferences(const ModuleRange &code, uintptr_t target)
{
    std::vector<ReferenceSite> sites;

    if (code.Size() < 8)
    {
        return sites;
    }

    const uint8_t *end = code.end;

    for (uint8_t *p = code.start; p + 4 <= end; p++)
    {
        uint32_t immediate;
        memcpy(&immediate, p, sizeof(immediate));

        if (immediate == static_cast<uint32_t>(target))
        {
            sites.push_back({ p, false, nullptr });
            continue;
        }

        int32_t displacement;
        memcpy(&displacement, p, sizeof(displacement));

        // RIP-relative operands are resolved from the end of the instruction. An
        // instruction may end right after the displacement (loads) or after a
        // further immediate (lea with a displacement, call/jmp rel32). Try both.
        for (size_t trailing = 0; trailing <= 4; trailing += 4)
        {
            uint8_t *instructionEnd = p + 4 + trailing;
            if (instructionEnd > end)
            {
                break;
            }

            const uintptr_t resolved =
                reinterpret_cast<uintptr_t>(instructionEnd) + static_cast<uintptr_t>(static_cast<intptr_t>(displacement));

            if (resolved == target)
            {
                sites.push_back({ p, true, instructionEnd });
                break;
            }
        }
    }

    return sites;
}

// The conditional branch immediately guarding 'site'.
struct GuardingBranch
{
    bool found{ false };
    uint8_t *address{ nullptr };
    bool isNear{ false }; // 0x0F 0x8x rel32 instead of 0x7x rel8
    bool skipsSite{ false };
};

// Walk back from 'site' to the nearest conditional branch and classify it. The
// branch must either land on the error block or jump past it; anything else means
// we are looking at something other than the validation guard.
GuardingBranch FindGuardingBranch(const ModuleRange &code, uint8_t *site)
{
    GuardingBranch result;

    uint8_t *windowStart = (site - kBranchSearchWindow > code.start) ? site - kBranchSearchWindow : code.start;

    for (uint8_t *p = site; p >= windowStart; p--)
    {
        // short Jcc: 0x70..0x7F, rel8
        if (*p >= 0x70 && *p <= 0x7F)
        {
            if (p + 2 > site)
            {
                continue;
            }

            const int8_t rel = static_cast<int8_t>(*(p + 1));
            uint8_t *target = p + 2 + rel;

            const bool jumpsToSite = (target == site);
            const bool jumpsPastSite = (target > site);

            if (!jumpsToSite && !jumpsPastSite)
            {
                continue;
            }

            result.found = true;
            result.address = p;
            result.isNear = false;
            result.skipsSite = jumpsPastSite;
            return result;
        }

        // near Jcc: 0x0F 0x80..0x8F, rel32
        if (*p == 0x0F && p + 6 <= site && *(p + 1) >= 0x80 && *(p + 1) <= 0x8F)
        {
            int32_t rel;
            memcpy(&rel, p + 2, sizeof(rel));
            uint8_t *target = p + 6 + rel;

            const bool jumpsToSite = (target == site);
            const bool jumpsPastSite = (target > site);

            if (!jumpsToSite && !jumpsPastSite)
            {
                continue;
            }

            result.found = true;
            result.address = p;
            result.isNear = true;
            result.skipsSite = jumpsPastSite;
            return result;
        }
    }

    return result;
}

} // namespace

bool Apply(bool verbose)
{
    ModuleRange image;
    if (!Platform::GetModuleImageRange(kModule, &image.start, &image.end))
    {
        if (verbose)
        {
            Platform::Print("panorama: module not loaded, cannot relax code.pbin check\n");
        }
        return false;
    }

    ModuleRange code;
    if (!Platform::GetModuleCodeSection(kModule, &code.start, &code.end))
    {
        if (verbose)
        {
            Platform::Print("panorama: no executable section found\n");
        }
        return false;
    }

    uint8_t *errorString = FindString(image, kInvalidDataError);
    if (!errorString)
    {
        if (verbose)
        {
            Platform::Print("panorama: invalid-data error string not found\n");
        }
        return false;
    }

    std::vector<ReferenceSite> references =
        FindReferences(code, reinterpret_cast<uintptr_t>(errorString));

    if (references.size() != 1)
    {
        if (verbose)
        {
            Platform::Print("panorama: expected exactly one reference to the invalid-data error, found %zu\n",
                references.size());
        }
        return false;
    }

    const ReferenceSite &reference = references.front();

    // The branch guards the block that reports the error, so it sits at or just
    // before the first instruction of that block.
    GuardingBranch branch = FindGuardingBranch(code, reference.immediate);
    if (!branch.found)
    {
        if (verbose)
        {
            Platform::Print("panorama: no guarding branch found for the invalid-data error\n");
        }
        return false;
    }

    // We can only make the error unconditional when the branch already skips it.
    // The opposite layout would require inverting the condition, which changes the
    // meaning of a flag we have not verified; refuse rather than guess.
    if (!branch.skipsSite)
    {
        if (verbose)
        {
            Platform::Print("panorama: invalid-data guard at %p reaches the error when taken; "
                            "refusing to invert an unverified condition\n",
                branch.address);
        }
        return false;
    }

    uint8_t patch[6];
    size_t patchSize = 0;

    if (!branch.isNear)
    {
        // 0x7x rel8 -> EB rel8
        patch[0] = 0xEB;
        patch[1] = *(branch.address + 1);
        patchSize = 2;
    }
    else
    {
        // 0x0F 0x8x rel32 -> E9 rel32
        patch[0] = 0xE9;
        memcpy(patch + 1, branch.address + 2, sizeof(int32_t));
        patchSize = 5;
    }

    if (!Platform::WriteToProtectedMemory(branch.address, patch, patchSize, true))
    {
        if (verbose)
        {
            Platform::Print("panorama: failed to patch the invalid-data guard at %p\n", branch.address);
        }
        return false;
    }

    if (verbose)
    {
        Platform::Print("panorama: relaxed code.pbin signature check\n");
    }

    return true;
}

} // namespace Panorama
