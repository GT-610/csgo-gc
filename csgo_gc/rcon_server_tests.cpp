#include "stdafx.h"
#include "gc_client.h"
#include "rcon_server.h"
#include "test_filesystem.h"

#include <cstdarg>
#include <cstdio>

// Test-only access to the connection-worker bookkeeping.
struct RconServerTests
{
    static void AddWorker(RconServer &server, bool finished)
    {
        auto flag = std::make_shared<std::atomic<bool>>(finished);
        std::lock_guard lock{ server.m_mutex };
        server.m_connectionThreads.push_back(RconServer::ConnectionWorker{
            std::thread{ [] {} }, flag });
    }

    static size_t WorkerCount(RconServer &server)
    {
        std::lock_guard lock{ server.m_mutex };
        return server.m_connectionThreads.size();
    }

    static void Reap(RconServer &server) { server.ReapFinishedConnectionThreads(); }

    static void ReleaseAll(RconServer &server) { server.JoinConnectionThreads(); }
};

namespace
{

std::mutex s_logMutex;
std::condition_variable s_logChanged;
std::vector<std::string> s_logMessages;

bool WriteConfig()
{
    if (!TestFilesystem::MakeDirectory("csgo_gc"))
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

bool FinishedConnectionThreadsAreReapedWithoutBlocking()
{
    RconServer server;
    RconServerTests::AddWorker(server, true);
    const size_t before = RconServerTests::WorkerCount(server);
    // Reaping a finished worker joins it and drops it from the live list.
    const auto started = std::chrono::steady_clock::now();
    RconServerTests::Reap(server);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const size_t after = RconServerTests::WorkerCount(server);
    if (before != 1 || after != 0) return false;
    if (elapsed > std::chrono::seconds{ 2 }) return false;

    // A worker that never sets its flag must be kept, not joined or dropped.
    RconServerTests::AddWorker(server, false);
    RconServerTests::Reap(server);
    const bool kept = RconServerTests::WorkerCount(server) == 1;
    RconServerTests::ReleaseAll(server);
    return kept;
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
    success &= FinishedConnectionThreadsAreReapedWithoutBlocking();

    TestFilesystem::RemoveFile("csgo_gc/config.txt");
    TestFilesystem::RemoveDirectory("csgo_gc");
    return success ? 0 : 1;
}
