#include "stdafx.h"
#include "tradeup_hook.h"
#include "config.h"
#include "platform.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <funchook.h>

namespace
{

// CEconCraftingRecipeDefinition::ItemListMatchesInputs in the final CS:GO
// client (thiscall, 2 stack args, ret 8). Found by pattern so it works on any
// build with the same code; the filter check is verified as well.
constexpr uint8_t FunctionSignature[] = {
    0x55,                               // push ebp
    0x8b, 0xec,                         // mov ebp, esp
    0x83, 0xec, 0x28,                   // sub esp, 0x28
    0x53,                               // push ebx
    0x8b, 0xd9,                         // mov ebx, ecx
    0xc6, 0x45, 0xff, 0x01,             // mov byte [ebp-1], 1
    0x56,                               // push esi
    0x57,                               // push edi
    0x8d, 0x4d, 0xd8,                   // lea ecx, [ebp-0x28]
    0x89, 0x5d, 0xf8,                   // mov [ebp-8], ebx
    0xff, 0xb3, 0x50, 0x05, 0x00, 0x00, // push dword [ebx+0x550]
};

// cmp dword [ebx+0x540], -3: the "trade-up recipe" check that guards the
// collection check
constexpr uint8_t FilterCheck[] = { 0x83, 0xbb, 0x40, 0x05, 0x00, 0x00, 0xfd };
constexpr size_t FilterCheckOffset = 0xa0;

// recipe definition layout in that build
constexpr size_t RecipeFilterOffset = 0x540; // int32, -3 for trade-up recipes
constexpr size_t RecipeInputCountOffset = 0x550; // int32, number of input groups
constexpr size_t RecipeInputDupCountsOffset = 0x558; // uint8_t *, items per input group

constexpr int32_t FilterTradeUp = -3;
constexpr int32_t FilterSkipCollectionCheck = -4; // any value other than -2/-3

constexpr uint8_t GoldContractInputCount = 5;

using ItemListMatchesInputsFn = bool(__fastcall *)(void *recipe, void *edx, void *items, bool allowPartial);
ItemListMatchesInputsFn s_originalItemListMatchesInputs;

bool s_hookDone;

// logs when a 5-item contract's answer changes, so a greyed-out PROCEED button
// can be told apart from "the hook never ran"
int s_lastContractResult = -1;
int s_contractLogs;

// the 5-item gold recipes are the only recipes with a single 5-item input group
bool IsGoldContractRecipe(const uint8_t *recipe)
{
    const int32_t inputGroups = *reinterpret_cast<const int32_t *>(recipe + RecipeInputCountOffset);
    const uint8_t *dupCounts = *reinterpret_cast<const uint8_t *const *>(recipe + RecipeInputDupCountsOffset);
    return inputGroups == 1 && dupCounts && dupCounts[0] == GoldContractInputCount;
}

bool __fastcall Hk_ItemListMatchesInputs(void *recipe, void *edx, void *items, bool allowPartial)
{
    uint8_t *recipeBytes = static_cast<uint8_t *>(recipe);
    int32_t &filter = *reinterpret_cast<int32_t *>(recipeBytes + RecipeFilterOffset);

    if (filter != FilterTradeUp || !IsGoldContractRecipe(recipeBytes))
    {
        return s_originalItemListMatchesInputs(recipe, edx, items, allowPartial);
    }

    // run the normal input matching without the "collection must have a next
    // rarity" check, then put the filter back
    filter = FilterSkipCollectionCheck;
    const bool matches = s_originalItemListMatchesInputs(recipe, edx, items, allowPartial);
    filter = FilterTradeUp;

    if (static_cast<int>(matches) != s_lastContractResult && s_contractLogs < 30)
    {
        s_lastContractResult = matches;
        s_contractLogs++;
        Platform::Print("Trade-up hook: 5-item contract %s (allowPartial=%d)\n",
            matches ? "matches" : "does NOT match", allowPartial ? 1 : 0);
    }
    return matches;
}

const uint8_t *FindInTextSection(HMODULE module, const uint8_t *pattern, size_t patternSize)
{
    auto base = reinterpret_cast<const uint8_t *>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return nullptr;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return nullptr;
    }

    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
    {
        if (memcmp(section->Name, ".text", 6) != 0)
        {
            continue;
        }

        const uint8_t *start = base + section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;
        if (size < patternSize)
        {
            return nullptr;
        }

        const uint8_t *found = nullptr;
        for (size_t offset = 0; offset + patternSize <= size; offset++)
        {
            if (start[offset] == pattern[0] && memcmp(start + offset, pattern, patternSize) == 0)
            {
                if (found)
                {
                    return nullptr; // not unique, don't guess
                }
                found = start + offset;
            }
        }
        return found;
    }

    return nullptr;
}

} // namespace

void TryInstallTradeUpHook()
{
    if (s_hookDone)
    {
        return;
    }

    if (!GetConfig().GoldTradeUpRecipes())
    {
        s_hookDone = true;
        return;
    }

    HMODULE client = GetModuleHandleA("client.dll");
    if (!client)
    {
        return; // not loaded yet, try again later
    }

    s_hookDone = true;

    const uint8_t *function = FindInTextSection(client, FunctionSignature, sizeof(FunctionSignature));
    if (!function || memcmp(function + FilterCheckOffset, FilterCheck, sizeof(FilterCheck)) != 0)
    {
        Platform::Print("Trade-up hook: this client.dll build is not supported, 5-item gold contracts "
            "won't show up in the trade-up screen\n");
        return;
    }

    funchook_t *funchook = funchook_create();
    if (!funchook)
    {
        Platform::Print("Trade-up hook: funchook_create failed\n");
        return;
    }

    void *target = const_cast<uint8_t *>(function);
    if (funchook_prepare(funchook, &target, reinterpret_cast<void *>(&Hk_ItemListMatchesInputs)) != 0)
    {
        Platform::Print("Trade-up hook: could not prepare the hook (%s)\n", funchook_error_message(funchook));
        funchook_destroy(funchook);
        return;
    }

    // the hook can run as soon as it's installed, so set the original first
    s_originalItemListMatchesInputs = reinterpret_cast<ItemListMatchesInputsFn>(target);

    if (funchook_install(funchook, 0) != 0)
    {
        Platform::Print("Trade-up hook: could not install the hook (%s)\n", funchook_error_message(funchook));
        funchook_destroy(funchook);
        return;
    }

    Platform::Print("Trade-up hook: installed, Coverts and knives/gloves can go into 5-item contracts\n");
}

#else

void TryInstallTradeUpHook()
{
}

#endif
