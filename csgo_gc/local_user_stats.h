#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace LocalUserStats
{

enum class LoadResult
{
    Success,
    NotFound,
    ReadError,
    InvalidFormat
};

constexpr const char *LocalPath = "csgo_gc/achievements.txt";

class Store
{
public:
    explicit Store(std::string path);

    LoadResult Load();
    bool IsLoaded() const { return m_loaded; }

    bool GetStat(std::string_view name, int32_t &value) const;
    bool GetStat(std::string_view name, float &value) const;
    bool SetStat(std::string_view name, int32_t value);
    bool SetStat(std::string_view name, float value);

    bool GetAchievement(std::string_view name, bool &achieved, uint32_t &unlockTime) const;
    bool SetAchievement(std::string_view name, uint32_t unlockTime);
    bool ClearAchievement(std::string_view name);

    bool Save(std::vector<std::string> &storedAchievements);
    bool ResetAllStats(bool achievementsToo);

private:
    static bool IsValidName(std::string_view name);

    std::string m_path;
    bool m_loaded{};
    std::unordered_map<std::string, int32_t> m_integerStats;
    std::unordered_map<std::string, float> m_floatStats;
    std::unordered_map<std::string, uint32_t> m_achievements;
    std::unordered_set<std::string> m_pendingAchievements;
};

} // namespace LocalUserStats
