#include "stdafx.h"
#include "local_user_stats.h"
#include "keyvalue.h"

#include <bit>
#include <filesystem>

namespace LocalUserStats
{

namespace
{

constexpr uint32_t FileVersion = 1;
constexpr size_t MaxNameLength = 127;

bool IsSerializableName(std::string_view name)
{
    if (name.empty() || name.size() > MaxNameLength)
    {
        return false;
    }

    for (unsigned char character : name)
    {
        if (character < ' ' || character == '"')
        {
            return false;
        }
    }
    return true;
}

template<typename T>
bool ParseNumber(std::string_view string, T &value)
{
    T parsed{};
    auto result = std::from_chars(string.data(), string.data() + string.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != string.data() + string.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

template<typename T>
std::vector<std::string> SortedNames(const std::unordered_map<std::string, T> &values)
{
    std::vector<std::string> names;
    names.reserve(values.size());
    for (const auto &[name, value] : values)
    {
        (void)value;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

Store::Store(std::string path)
    : m_path{ std::move(path) }
{
}

LoadResult Store::Load()
{
    KeyValue root{ "user_stats" };
    KeyValueFileResult fileResult = root.ParseFromFileDetailed(m_path.c_str());
    if (fileResult == KeyValueFileResult::NotFound)
    {
        m_integerStats.clear();
        m_floatStats.clear();
        m_achievements.clear();
        m_pendingAchievements.clear();
        m_loaded = true;
        return LoadResult::NotFound;
    }
    if (fileResult == KeyValueFileResult::ReadError)
    {
        return LoadResult::ReadError;
    }
    if (fileResult != KeyValueFileResult::Success)
    {
        return LoadResult::InvalidFormat;
    }

    uint32_t version{};
    const KeyValue *versionValue = root.GetSubkey("version");
    if (!versionValue || !ParseNumber(versionValue->String(), version) || version != FileVersion)
    {
        return LoadResult::InvalidFormat;
    }

    std::unordered_map<std::string, int32_t> integerStats;
    std::unordered_map<std::string, float> floatStats;
    std::unordered_map<std::string, uint32_t> achievements;
    if (const KeyValue *section = root.GetSubkey("integer_stats"))
    {
        for (const KeyValue &entry : *section)
        {
            int32_t value{};
            if (!IsSerializableName(entry.Name()) || !ParseNumber(entry.String(), value))
            {
                return LoadResult::InvalidFormat;
            }
            if (!integerStats.emplace(entry.Name(), value).second)
            {
                return LoadResult::InvalidFormat;
            }
        }
    }

    if (const KeyValue *section = root.GetSubkey("float_stats"))
    {
        for (const KeyValue &entry : *section)
        {
            uint32_t bits{};
            if (!IsSerializableName(entry.Name()) || !ParseNumber(entry.String(), bits)
                || integerStats.contains(std::string{ entry.Name() }))
            {
                return LoadResult::InvalidFormat;
            }
            if (!floatStats.emplace(entry.Name(), std::bit_cast<float>(bits)).second)
            {
                return LoadResult::InvalidFormat;
            }
        }
    }

    if (const KeyValue *section = root.GetSubkey("achievements"))
    {
        for (const KeyValue &entry : *section)
        {
            uint32_t unlockTime{};
            if (!IsSerializableName(entry.Name()) || !ParseNumber(entry.String(), unlockTime))
            {
                return LoadResult::InvalidFormat;
            }
            if (!achievements.emplace(entry.Name(), unlockTime).second)
            {
                return LoadResult::InvalidFormat;
            }
        }
    }

    m_integerStats = std::move(integerStats);
    m_floatStats = std::move(floatStats);
    m_achievements = std::move(achievements);
    m_pendingAchievements.clear();
    m_loaded = true;
    return LoadResult::Success;
}

bool Store::IsValidName(std::string_view name)
{
    return IsSerializableName(name);
}

bool Store::GetStat(std::string_view name, int32_t &value) const
{
    if (!m_loaded || !IsValidName(name) || m_floatStats.contains(std::string{ name }))
    {
        return false;
    }
    auto it = m_integerStats.find(std::string{ name });
    value = it == m_integerStats.end() ? 0 : it->second;
    return true;
}

bool Store::GetStat(std::string_view name, float &value) const
{
    if (!m_loaded || !IsValidName(name) || m_integerStats.contains(std::string{ name }))
    {
        return false;
    }
    auto it = m_floatStats.find(std::string{ name });
    value = it == m_floatStats.end() ? 0.0f : it->second;
    return true;
}

bool Store::SetStat(std::string_view name, int32_t value)
{
    if (!m_loaded || !IsValidName(name) || m_floatStats.contains(std::string{ name }))
    {
        return false;
    }
    m_integerStats[std::string{ name }] = value;
    return true;
}

bool Store::SetStat(std::string_view name, float value)
{
    if (!m_loaded || !IsValidName(name) || m_integerStats.contains(std::string{ name }))
    {
        return false;
    }
    m_floatStats[std::string{ name }] = value;
    return true;
}

bool Store::GetAchievement(std::string_view name, bool &achieved, uint32_t &unlockTime) const
{
    if (!m_loaded || !IsValidName(name))
    {
        return false;
    }
    auto it = m_achievements.find(std::string{ name });
    achieved = it != m_achievements.end();
    unlockTime = achieved ? it->second : 0;
    return true;
}

bool Store::SetAchievement(std::string_view name, uint32_t unlockTime)
{
    if (!m_loaded || !IsValidName(name))
    {
        return false;
    }

    std::string ownedName{ name };
    if (m_achievements.emplace(ownedName, unlockTime).second)
    {
        m_pendingAchievements.emplace(std::move(ownedName));
    }
    return true;
}

bool Store::ClearAchievement(std::string_view name)
{
    if (!m_loaded || !IsValidName(name))
    {
        return false;
    }
    std::string ownedName{ name };
    m_achievements.erase(ownedName);
    m_pendingAchievements.erase(ownedName);
    return true;
}

bool Store::Save(std::vector<std::string> &storedAchievements)
{
    if (!m_loaded)
    {
        return false;
    }

    std::filesystem::path parent = std::filesystem::path{ m_path }.parent_path();
    std::error_code error;
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            return false;
        }
    }

    KeyValue root{ "user_stats" };
    root.AddNumber("version", FileVersion);

    if (!m_integerStats.empty())
    {
        KeyValue &section = root.AddSubkey("integer_stats");
        for (const std::string &name : SortedNames(m_integerStats))
        {
            section.AddNumber(name, m_integerStats.at(name));
        }
    }

    if (!m_floatStats.empty())
    {
        KeyValue &section = root.AddSubkey("float_stats");
        for (const std::string &name : SortedNames(m_floatStats))
        {
            section.AddNumber(name, std::bit_cast<uint32_t>(m_floatStats.at(name)));
        }
    }

    if (!m_achievements.empty())
    {
        KeyValue &section = root.AddSubkey("achievements");
        for (const std::string &name : SortedNames(m_achievements))
        {
            section.AddNumber(name, m_achievements.at(name));
        }
    }

    if (!root.WriteToFile(m_path.c_str()))
    {
        return false;
    }

    storedAchievements.assign(m_pendingAchievements.begin(), m_pendingAchievements.end());
    std::sort(storedAchievements.begin(), storedAchievements.end());
    m_pendingAchievements.clear();
    return true;
}

bool Store::ResetAllStats(bool achievementsToo)
{
    if (!m_loaded)
    {
        return false;
    }
    m_integerStats.clear();
    m_floatStats.clear();
    if (achievementsToo)
    {
        m_achievements.clear();
        m_pendingAchievements.clear();
    }
    return true;
}

} // namespace LocalUserStats
