#include "stdafx.h"
#include "gc_client.h"
#include "rcon_server.h"

#include <cstdarg>
#include <cstdio>
#include <filesystem>

namespace
{

std::mutex s_logMutex;
std::condition_variable s_logChanged;
std::vector<std::string> s_logMessages;

bool WriteConfig()
{
    std::error_code error;
    std::filesystem::create_directories("csgo_gc", error);
    if (error)
    {
        return false;
    }

    constexpr std::string_view config{
        "\"rcon\"\n"
        "{\n"
        "    \"enabled\" \"1\"\n"
        "    \"bind_address\" \"192.0.2.1\"\n"
        "    \"port\" \"37016\"\n"
        "    \"password\" \"test-password\"\n"
        "}\n"
    };

    FILE *file = fopen("csgo_gc/config.txt", "wb");
    if (!file)
    {
        return false;
    }

    bool success = fwrite(config.data(), 1, config.size(), file) == config.size();
    success &= fclose(file) == 0;
    return success;
}

bool WaitForLog(std::string_view fragment)
{
    std::unique_lock lock{ s_logMutex };
    return s_logChanged.wait_for(lock, std::chrono::seconds{ 5 }, [fragment] {
        return std::any_of(s_logMessages.begin(), s_logMessages.end(), [fragment](const std::string &message) {
            return message.find(fragment) != std::string::npos;
        });
    });
}

size_t CountLogs(std::string_view fragment)
{
    std::lock_guard lock{ s_logMutex };
    return static_cast<size_t>(std::count_if(s_logMessages.begin(), s_logMessages.end(), [fragment](const std::string &message) {
        return message.find(fragment) != std::string::npos;
    }));
}

bool ListenerFailureDisablesRconWithoutRetrying()
{
    if (!WriteConfig())
    {
        return false;
    }

    RconServer server;
    server.Start();
    if (!WaitForLog("RCON: bind failed on 192.0.2.1:37016"))
    {
        return false;
    }

    server.RegisterClient(nullptr);
    server.Stop();

    return CountLogs("RCON: starting listener thread") == 1
        && CountLogs("RCON: disabled for this process after listener failure") == 1;
}

} // namespace

namespace Platform
{

void Print(const char *format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    {
        std::lock_guard lock{ s_logMutex };
        s_logMessages.emplace_back(buffer);
    }
    s_logChanged.notify_all();
}

} // namespace Platform

std::string ClientGC::RunRconCommand(std::string)
{
    return {};
}

std::string ClientGC::RconCommandUsageList()
{
    return {};
}

int main()
{
    bool success = ListenerFailureDisablesRconWithoutRetrying();

    std::error_code error;
    std::filesystem::remove_all("csgo_gc", error);
    return success ? 0 : 1;
}
