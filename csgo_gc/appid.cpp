#include "stdafx.h"
#include "appid.h"
#include "keyvalue.h" // LoadFile
#include "config.h"

namespace AppId
{

constexpr uint32_t DefaultBuildYear = 2023;

static size_t ifind(std::string_view haystack, std::string_view needle)
{
    // not needed here but keep it
    if (needle.empty() || haystack.size() < needle.size())
    {
        return std::string::npos;
    }

    auto pred = [](unsigned char a, unsigned char b)
    { return std::tolower(a) == std::tolower(b); };

    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), pred);
    return (it != haystack.end()) ? std::distance(haystack.begin(), it) : std::string::npos;
}

enum class SteamInfResult
{
    Ok,
    InvalidFile,
    AlreadyOk
};

static SteamInfResult ReplaceSteamInfAppId(std::string &buf, uint32_t appId)
{
    size_t keyPos = ifind(buf, "appid=");
    if (keyPos == std::string::npos)
    {
        return SteamInfResult::InvalidFile;
    }

    size_t valuePos = keyPos + sizeof("appid=") - 1;

    uint32_t existing = 0;
    auto [ptr, ec] = std::from_chars(buf.data() + valuePos, buf.data() + buf.size(), existing);
    if (ec != std::errc{})
    {
        return SteamInfResult::InvalidFile;
    }

    if (existing == appId)
    {
        return SteamInfResult::AlreadyOk;
    }

    size_t valueLen = ptr - (buf.data() + valuePos);
    std::string newValue = std::to_string(appId);
    buf.replace(valuePos, valueLen, newValue);

    return SteamInfResult::Ok;
}

static uint32_t ParseSteamInfBuildYear(std::string_view buf)
{
    size_t keyPos = ifind(buf, "VersionDate=");
    if (keyPos == std::string::npos)
    {
        return 0;
    }

    size_t valuePos = keyPos + sizeof("VersionDate=") - 1;
    size_t valueEnd = buf.find_first_of("\r\n", valuePos);
    if (valueEnd == std::string_view::npos)
    {
        valueEnd = buf.size();
    }

    std::string_view value = buf.substr(valuePos, valueEnd - valuePos);
    for (size_t i = 0; i + 4 <= value.size(); i++)
    {
        bool fourDigits = true;
        for (size_t digit = 0; digit < 4; digit++)
        {
            fourDigits &= value[i + digit] >= '0' && value[i + digit] <= '9';
        }

        if (!fourDigits)
        {
            continue;
        }

        const bool startsAtBoundary = i == 0 || value[i - 1] < '0' || value[i - 1] > '9';
        const bool endsAtBoundary = i + 4 == value.size()
            || value[i + 4] < '0' || value[i + 4] > '9';
        if (!startsAtBoundary || !endsAtBoundary)
        {
            continue;
        }

        uint32_t year = 0;
        auto [ptr, ec] = std::from_chars(value.data() + i, value.data() + i + 4, year);
        if (ec == std::errc{} && ptr == value.data() + i + 4
            && year >= 2012 && year <= DefaultBuildYear)
        {
            return year;
        }
    }

    return 0;
}

static void WriteFile(const char *path, const std::string &buffer)
{
    FILE *f = fopen(path, "wb");
    if (f)
    {
        fwrite(buffer.data(), 1, buffer.size(), f);
        fclose(f);
    }
}

void Init()
{
    // defaults to 730 if not specified
    uint32_t appIdOverride = GetConfig().AppIdOverride();

    Platform::Print("Using app id %u\n", appIdOverride);

    std::string steamInf = LoadFile("csgo/steam.inf");
    SteamInfResult steamInfResult = ReplaceSteamInfAppId(steamInf, appIdOverride);

    switch (steamInfResult)
    {
    case SteamInfResult::Ok:
        // could make a backup, but don't...
        WriteFile("csgo/steam.inf", steamInf);
        Platform::Print("Replaced steam.inf app id with %u\n", appIdOverride);
        break;

    case SteamInfResult::InvalidFile:
        Platform::Print("Did not replace steam.inf app id with %u (invalid file)\n", appIdOverride);
        break;

    case SteamInfResult::AlreadyOk:
        Platform::Print("steam.inf app id was already set to %u\n", appIdOverride);
        break;
    }
}

uint32_t GetOverride()
{
    return GetConfig().AppIdOverride();
}

uint32_t GetBuildYear()
{
    uint32_t buildYear = ParseSteamInfBuildYear(LoadFile("csgo/steam.inf"));
    if (!buildYear)
    {
        Platform::Print("Could not read the client build year from steam.inf; using %u\n",
            DefaultBuildYear);
        return DefaultBuildYear;
    }

    return buildYear;
}

bool IsOriginal()
{
    return (GetConfig().AppIdOverride() == 730);
}

}
