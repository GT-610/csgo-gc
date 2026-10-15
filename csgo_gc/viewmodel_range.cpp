#include "stdafx.h"
#include "viewmodel_range.h"
#include "config.h"
#include "platform.h"

// ConVar layout in the final CS:GO client (32-bit): the ConCommandBase part is
// 0x18 bytes with the name pointer at 0xc, then the IConVar part. The range
// sits after the value.
namespace
{
constexpr size_t NameOffset = 0xc;
constexpr size_t HasMinOffset = 0x34;
constexpr size_t MinOffset = 0x38;
constexpr size_t HasMaxOffset = 0x3c;
constexpr size_t MaxOffset = 0x40;

constexpr const char *ViewmodelConVars[] = { "viewmodel_offset_x", "viewmodel_offset_y", "viewmodel_offset_z" };
} // namespace

bool ScaleViewmodelRange(void *convar, float multiplier)
{
    auto bytes = static_cast<uint8_t *>(convar);
    float minimum, maximum;
    memcpy(&minimum, bytes + MinOffset, sizeof(minimum));
    memcpy(&maximum, bytes + MaxOffset, sizeof(maximum));

    // a ConVar with both limits set, around zero: anything else isn't one of ours
    const bool hasMin = bytes[HasMinOffset] == 1;
    const bool hasMax = bytes[HasMaxOffset] == 1;
    if (!hasMin || !hasMax || !(minimum < 0.0f) || !(maximum > 0.0f) || minimum < -1000.0f || maximum > 1000.0f
        || !(multiplier > 0.0f) || multiplier > 1000.0f)
    {
        return false;
    }

    minimum *= multiplier;
    maximum *= multiplier;
    memcpy(bytes + MinOffset, &minimum, sizeof(minimum));
    memcpy(bytes + MaxOffset, &maximum, sizeof(maximum));
    return true;
}

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
bool s_done;

struct Range
{
    uint8_t *start;
    size_t size;
};

std::vector<Range> Sections(HMODULE module, bool writableOnly)
{
    std::vector<Range> ranges;
    auto base = reinterpret_cast<uint8_t *>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return ranges;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
    {
        const bool writable = (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        if (writable == writableOnly)
        {
            ranges.push_back({ base + section->VirtualAddress, section->Misc.VirtualSize });
        }
    }
    return ranges;
}
} // namespace

void TryApplyViewmodelRange()
{
    if (s_done)
    {
        return;
    }

    const float multiplier = GetConfig().ViewmodelRange();
    if (multiplier == 1.0f)
    {
        s_done = true;
        return;
    }

    HMODULE client = GetModuleHandleA("client.dll");
    if (!client)
    {
        return; // not loaded yet
    }

    s_done = true;

    const std::vector<Range> readOnly = Sections(client, false);
    const std::vector<Range> writable = Sections(client, true);

    int widened = 0;
    for (const char *name : ViewmodelConVars)
    {
        const size_t nameLength = strlen(name) + 1;

        // the name string, then every ConVar whose name pointer points at it
        for (const Range &range : readOnly)
        {
            for (size_t i = 0; i + nameLength <= range.size; i++)
            {
                if (range.start[i] != static_cast<uint8_t>(name[0]) || memcmp(range.start + i, name, nameLength) != 0)
                {
                    continue;
                }

                const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(range.start + i));
                for (const Range &data : writable)
                {
                    for (size_t j = 0; j + sizeof(uint32_t) <= data.size; j += sizeof(uint32_t))
                    {
                        uint32_t value;
                        memcpy(&value, data.start + j, sizeof(value));
                        if (value != address || j < NameOffset)
                        {
                            continue;
                        }

                        uint8_t *convar = data.start + j - NameOffset;
                        if (convar + MaxOffset + sizeof(float) <= data.start + data.size
                            && ScaleViewmodelRange(convar, multiplier))
                        {
                            widened++;
                        }
                    }
                }
            }
        }
    }

    if (widened)
    {
        Platform::Print("Viewmodel: widened the range of %d viewmodel_offset setting%s by %gx\n", widened,
            widened == 1 ? "" : "s", multiplier);
    }
    else
    {
        Platform::Print("Viewmodel: couldn't find the viewmodel_offset settings in this client.dll, range unchanged\n");
    }
}

#else

void TryApplyViewmodelRange()
{
}

#endif
