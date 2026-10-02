#include "stdafx.h"
#include "config.h"
#include <cstdio>

namespace
{
std::atomic<unsigned> printCalls{};
std::atomic<unsigned> emittedWarnings{};
std::atomic<bool> fullyInitialized{ true };
bool expectCustomValues{};
}

namespace Platform
{
void Print(const char *, ...)
{
    // Match the production logger's dependency, including recursive GetConfig().
    const GCConfig &config = GetConfig();
    printCalls.fetch_add(1, std::memory_order_relaxed);
    if (config.GetLogOutput() > LogOutputNone)
    {
        emittedWarnings.fetch_add(1, std::memory_order_relaxed);
    }
    if (expectCustomValues && (config.AppIdOverride() != 12345 || config.Xp() != 987))
    {
        fullyInitialized.store(false, std::memory_order_relaxed);
    }
}
}

int main(int argc, char **argv)
{
    const bool invalidMusicKit = argc > 1 && std::string_view{ argv[1] } == "invalid";
    expectCustomValues = invalidMusicKit;
    std::vector<std::thread> readers;
    for (int i = 0; i < 8; ++i)
    {
        readers.emplace_back([] { (void)GetConfig(); });
    }
    for (std::thread &reader : readers)
    {
        reader.join();
    }
    const GCConfig &config = GetConfig();
    const unsigned expectedCalls = invalidMusicKit ? 1 : 0;
    const unsigned expectedEmitted = invalidMusicKit && config.GetLogOutput() > LogOutputNone ? 1 : 0;
    const bool valid = printCalls.load() == expectedCalls
        && emittedWarnings.load() == expectedEmitted
        && fullyInitialized.load()
        && config.MusicKitStatTrakGate() == MusicKit::StatTrakGate::CompetitiveRuleset;
    std::printf("Config initialization: %s (calls=%u, emitted=%u)\n",
        valid ? "PASS" : "FAIL", printCalls.load(), emittedWarnings.load());
    return valid ? 0 : 1;
}
