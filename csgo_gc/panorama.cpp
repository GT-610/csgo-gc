#include "stdafx.h"
#include "panorama.h"
#include "config.h"
#include "platform.h"

#include <cstring>

// Instruction decoding, used to only ever patch a real instruction boundary.
// funchook already ships diStorm3, so this adds no new dependency.
#include <distorm.h>
#include <mnemonics.h>


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

// Find a NUL-terminated string by content across readable intervals.
//
// The intervals are the module's real sections or loadable segments, so this never
// touches the padding between them. That padding belongs to no section and is not
// guaranteed to be mapped, so treating the whole image as one contiguous buffer
// could fault on a module whose segments are separated by unmapped pages.
uint8_t *FindString(const std::vector<std::pair<uint8_t *, uint8_t *>> &ranges, const char *value)
{
    const size_t length = strlen(value) + 1;
    const uint8_t *needle = reinterpret_cast<const uint8_t *>(value);

    for (const auto &range : ranges)
    {
        uint8_t *start = range.first;
        uint8_t *end = range.second;

        if (static_cast<size_t>(end - start) < length)
        {
            continue;
        }

        uint8_t *limit = end - length;

        for (uint8_t *p = start; p <= limit; p++)
        {
            if (memcmp(p, needle, length) == 0)
            {
                return p;
            }
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

// A decomposed instruction in the executable range.
struct DecodedInstruction
{
    uint8_t *address{ nullptr };
    uint8_t size{ 0 };
    uint8_t flowControl{ 0 };
    int64_t displacement{ 0 }; // branch displacement, relative to the end
};

// Decompose [code.start, code.end) into whole instructions.
//
// Decoding the section once, forward from its start, is what makes the instruction
// boundaries trustworthy. Decoding backwards from an arbitrary byte would happily
// produce "instructions" that begin in the middle of a real one, and patching such
// a location would corrupt the instruction it lands in.
//
// The decode width has to match the module being read. Every shipped CS:GO
// Panorama build is 32-bit, but guessing the width would silently mis-decode a
// module of the other kind, so the caller states which architecture it is looking
// at and this refuses to decode anything else.
bool DecomposeCode(const ModuleRange &code, _DecodeType width, std::vector<DecodedInstruction> &out)
{
    uint8_t *p = code.start;

    while (p < code.end)
    {
        _CodeInfo info;
        memset(&info, 0, sizeof(info));
        info.codeOffset = static_cast<_OffsetType>(reinterpret_cast<uintptr_t>(p));
        info.code = reinterpret_cast<const unsigned char *>(p);
        info.codeLen = static_cast<int>(code.end - p);
        info.dt = width;
        info.features = DF_NONE;

        _DInst instruction;
        unsigned int used = 0;
        const _DecodeResult status = distorm_decompose(&info, &instruction, 1, &used);

        // DECRES_MEMORYERR is the normal result when the result array holds a single
        // entry, so only an outright input error stops the walk.
        if (status == DECRES_INPUTERR || used != 1 || instruction.size == 0)
        {
            return false;
        }

        DecodedInstruction decoded;
        decoded.address = p;
        decoded.size = instruction.size;
        decoded.flowControl = static_cast<uint8_t>(META_GET_FC(instruction.meta));
        decoded.displacement = static_cast<int64_t>(static_cast<int32_t>(instruction.imm.dword));
        out.push_back(decoded);

        p += instruction.size;
    }

    return !out.empty();
}

// The condition that guards the error-report block, if it can be identified.
struct GuardingBranch
{
    bool found{ false };
    uint8_t *address{ nullptr };
    size_t size{ 0 };
    bool skipsBlock{ false };
};

// The single conditional branch landing on 'address', if there is exactly one.
bool FindBranchLandingOn(const std::vector<DecodedInstruction> &decoded, uint8_t *address,
    GuardingBranch &branch)
{
    bool any = false;

    for (const DecodedInstruction &instruction : decoded)
    {
        if (instruction.flowControl != FC_CND_BRANCH)
        {
            continue;
        }

        uint8_t *end = instruction.address + instruction.size;
        if (end != address)
        {
            continue;
        }

        if (any)
        {
            // Two branches land on the same place; we cannot tell which one gates
            // the error, so the caller must refuse.
            return false;
        }

        uint8_t *target = end + instruction.displacement;

        branch.found = true;
        branch.address = instruction.address;
        branch.size = instruction.size;
        branch.skipsBlock = (target > address);
        any = true;
    }

    return any;
}

// Locate the conditional branch that decides whether the Error() call reporting an
// invalid package runs.
//
// The reference to the error literal sits in the middle of the block that builds
// the Error() arguments, so the block start is found by walking backwards over that
// setup until a conditional branch lands there. Walking backwards - rather than
// assuming the literal's own instruction starts the block - keeps this independent
// of how many arguments the compiler passes and in which order it sets them up.
GuardingBranch FindGuardingBranch(const std::vector<DecodedInstruction> &decoded, uint8_t *reference)
{
    GuardingBranch result;

    if (decoded.empty())
    {
        return result;
    }

    size_t referenceIndex = decoded.size();
    for (size_t i = 0; i < decoded.size(); i++)
    {
        const DecodedInstruction &instruction = decoded[i];
        if (reference >= instruction.address && reference < instruction.address + instruction.size)
        {
            referenceIndex = i;
            break;
        }
    }

    if (referenceIndex == decoded.size())
    {
        return result;
    }

    const size_t limit = (referenceIndex > kBranchSearchWindow) ? referenceIndex - kBranchSearchWindow : 0;

    for (size_t index = referenceIndex;; index--)
    {
        GuardingBranch candidate;
        if (FindBranchLandingOn(decoded, decoded[index].address, candidate))
        {
            return candidate;
        }

        if (index == limit || index == 0)
        {
            break;
        }

        // Stop at anything that cannot be part of a straight-line argument setup.
        const uint8_t flow = decoded[index - 1].flowControl;
        if (flow == FC_CND_BRANCH || flow == FC_UNC_BRANCH || flow == FC_RET || flow == FC_SYS)
        {
            break;
        }
    }

    return result;
}

} // namespace

bool Apply(bool verbose)
{
    std::vector<std::pair<uint8_t *, uint8_t *>> ranges;
    if (!Platform::GetModuleReadableRanges(kModule, ranges))
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

    // The decode width has to match the module; a mismatch silently produces
    // wrong instruction boundaries, which is worse than refusing.
    const int bitness = Platform::GetModuleBitness(kModule);
    if (bitness != 32 && bitness != 64)
    {
        if (verbose)
        {
            Platform::Print("panorama: unsupported module architecture (%d-bit)\n", bitness);
        }
        return false;
    }

    const _DecodeType decodeWidth = (bitness == 64) ? Decode64Bits : Decode32Bits;

    uint8_t *errorString = FindString(ranges, kInvalidDataError);
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

    // Decompose once; the guard search needs real instruction boundaries.
    std::vector<DecodedInstruction> decoded;
    if (!DecomposeCode(code, decodeWidth, decoded))
    {
        if (verbose)
        {
            Platform::Print("panorama: could not decode the module's code section\n");
        }
        return false;
    }

    GuardingBranch branch = FindGuardingBranch(decoded, reference.immediate);
    if (!branch.found)
    {
        if (verbose)
        {
            Platform::Print("panorama: could not identify a single guard for the invalid-data error\n");
        }
        return false;
    }

    // We can only make the error unconditional when the branch already skips it.
    // The opposite layout would require inverting the condition, which changes the
    // meaning of a flag we have not verified; refuse rather than guess.
    if (!branch.skipsBlock)
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

    if (branch.size == 2)
    {
        // 0x7x rel8 -> EB rel8. Both forms end at the same address, so the
        // displacement carries over unchanged.
        patch[0] = 0xEB;
        patch[1] = *(branch.address + 1);
        patchSize = 2;
    }
    else if (branch.size >= 5 && *(branch.address) == 0x0F
        && (*(branch.address + 1) & 0xF0) == 0x80)
    {
        // 0x0F 0x8x rel32 (6 bytes) -> E9 rel32 (5 bytes). The two forms do not end
        // at the same address, so the displacement cannot be copied: it has to be
        // recomputed against the shorter instruction, or the jump would land one
        // byte before the intended target.
        const int32_t original = *reinterpret_cast<const int32_t *>(branch.address + 2);
        const uint8_t *target = branch.address + branch.size + original;
        const int32_t adjusted = static_cast<int32_t>(target - (branch.address + 5));

        patch[0] = 0xE9;
        memcpy(patch + 1, &adjusted, sizeof(adjusted));
        patchSize = 5;
    }
    else
    {
        if (verbose)
        {
            Platform::Print("panorama: unsupported guard encoding at %p (size %zu)\n",
                branch.address, branch.size);
        }
        return false;
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
