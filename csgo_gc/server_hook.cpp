#include "stdafx.h"
#include "server_hook.h"
#include "bot_skins.h"
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

// CCSPlayer::GiveNamedItem in the final CS:GO server.dll (thiscall, 5 stack
// args, ret 0x14, returns the new entity). Found by its first bytes, which are
// unique, and checked further in.
constexpr uint8_t FunctionSignature[] = {
    0x55,                                           // push ebp
    0x8b, 0xec,                                     // mov ebp, esp
    0x83, 0xe4, 0xf8,                               // and esp, -8
    0x83, 0xec, 0x64,                               // sub esp, 0x64
    0x53,                                           // push ebx
    0x8b, 0x5d, 0x08,                               // mov ebx, [ebp+8]   (item name)
    0x56,                                           // push esi
    0x57,                                           // push edi
    0x8b, 0xf9,                                     // mov edi, ecx
    0x85, 0xdb,                                     // test ebx, ebx
    0x0f, 0x84,                                     // je ...
};

// cmp byte [ebx], 0 / je: the empty-name check
constexpr uint8_t NameCheck[] = { 0x80, 0x3b, 0x00, 0x0f, 0x84 };
constexpr size_t NameCheckOffset = 25;

// offsets in that build (taken from the server's own network tables)
constexpr size_t PlayerFlagsOffset = 0xd8; // m_fFlags
constexpr uint32_t FlagFakeClient = 1u << 9;

constexpr size_t ItemIdHighOffset = 0x710 + 0x68; // m_AttributeManager.m_Item.m_iItemIDHigh
constexpr size_t EntityQualityOffset = 0x710 + 0x58; // m_AttributeManager.m_Item.m_iEntityQuality
constexpr size_t OriginalOwnerXuidLowOffset = 0x910;
constexpr size_t OriginalOwnerXuidHighOffset = 0x914;
constexpr size_t FallbackPaintKitOffset = 0x918;
constexpr size_t FallbackSeedOffset = 0x91c;
constexpr size_t FallbackWearOffset = 0x920;
constexpr size_t FallbackStatTrakOffset = 0x924;

using GiveNamedItemFn = void *(__fastcall *)(void *player, void *edx, const char *name, int subType,
    void *scriptItem, bool force, void *origin);
GiveNamedItemFn s_originalGiveNamedItem;

bool s_hookDone;
uint32_t s_skinsGiven;

template <typename T>
T &Field(void *entity, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(entity) + offset);
}

void *__fastcall Hk_GiveNamedItem(void *player, void *edx, const char *name, int subType,
    void *scriptItem, bool force, void *origin)
{
    const bool isBot = player && name && (Field<uint32_t>(player, PlayerFlagsOffset) & FlagFakeClient);
    // what the game asks for, for the first few humans and the first bot calls
    static int s_loggedHuman;
    static int s_loggedBot;
    const char *requested = name;
    const bool logThis = name && (isBot ? s_loggedBot < 60 : s_loggedHuman < 3);
    if (logThis)
    {
        (isBot ? s_loggedBot : s_loggedHuman)++;
    }
    const uint64_t botKey = reinterpret_cast<uintptr_t>(player);

    // a bot's plain knife is swapped for a themed one
    if (isBot && GetConfig().BotSkins().knives)
    {
        if (const char *knife = BotSkins::KnifeFor(name, botKey))
        {
            name = knife;
        }
    }

    void *weapon = s_originalGiveNamedItem(player, edx, name, subType, scriptItem, force, origin);

    if (logThis)
    {
        Platform::Print("Bot skins: %s %p asked for %s%s%s -> %s\n", isBot ? "bot" : "human", player, requested,
            name != requested ? ", given " : "", name != requested ? name : "",
            weapon ? "a weapon was created" : "nothing created");
    }

    if (!weapon || !isBot)
    {
        return weapon;
    }

    BotSkins::Skin skin;
    if (!BotSkins::Pick(name, botKey, skin))
    {
        return weapon;
    }

    // "-1" as the item id tells clients to use the fallback skin fields
    Field<int32_t>(weapon, ItemIdHighOffset) = -1;
    Field<uint32_t>(weapon, OriginalOwnerXuidLowOffset) = 0;
    Field<uint32_t>(weapon, OriginalOwnerXuidHighOffset) = 0;
    Field<uint32_t>(weapon, FallbackPaintKitOffset) = skin.paintKit;
    Field<uint32_t>(weapon, FallbackSeedOffset) = skin.seed;
    Field<float>(weapon, FallbackWearOffset) = skin.wear;
    Field<int32_t>(weapon, FallbackStatTrakOffset) = -1;
    if (std::string_view{ name }.rfind("weapon_knife", 0) == 0 || std::string_view{ name } == "weapon_bayonet")
    {
        Field<int32_t>(weapon, EntityQualityOffset) = 3; // Unusual, like a real knife skin
    }

    if (++s_skinsGiven <= 5)
    {
        Platform::Print("Bot skins: %s theme, %s got paint kit %u (seed %u, wear %.3f)\n",
            BotSkins::ThemeName(botKey), name, skin.paintKit, skin.seed, skin.wear);
    }

    return weapon;
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

void TryInstallServerHook()
{
    if (s_hookDone)
    {
        return;
    }

    if (!GetConfig().BotSkins().enabled)
    {
        s_hookDone = true;
        return;
    }

    HMODULE server = GetModuleHandleA("server.dll");
    if (!server)
    {
        return; // not loaded yet, try again later
    }

    s_hookDone = true;

    const uint8_t *function = FindInTextSection(server, FunctionSignature, sizeof(FunctionSignature));
    if (!function || memcmp(function + NameCheckOffset, NameCheck, sizeof(NameCheck)) != 0)
    {
        Platform::Print("Bot skins: this server.dll build is not supported, bots keep their default skins\n");
        return;
    }

    funchook_t *funchook = funchook_create();
    if (!funchook)
    {
        Platform::Print("Bot skins: funchook_create failed\n");
        return;
    }

    void *target = const_cast<uint8_t *>(function);
    if (funchook_prepare(funchook, &target, reinterpret_cast<void *>(&Hk_GiveNamedItem)) != 0)
    {
        Platform::Print("Bot skins: could not prepare the hook (%s)\n", funchook_error_message(funchook));
        funchook_destroy(funchook);
        return;
    }

    // the hook can run as soon as it's installed, so set the original first
    s_originalGiveNamedItem = reinterpret_cast<GiveNamedItemFn>(target);

    if (funchook_install(funchook, 0) != 0)
    {
        Platform::Print("Bot skins: could not install the hook (%s)\n", funchook_error_message(funchook));
        funchook_destroy(funchook);
        return;
    }

    Platform::Print("Bot skins: installed, bots get random popular skins\n");
}

#else

void TryInstallServerHook()
{
}

#endif
