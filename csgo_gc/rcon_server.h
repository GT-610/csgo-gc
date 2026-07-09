#pragma once

class ClientGC;

class RconServer
{
public:
    RconServer();
    ~RconServer();

    void Start();
    void Stop();

    void RegisterClient(ClientGC *client);
    void UnregisterClient(ClientGC *client);

private:
    void ThreadMain();
    void HandleConnection(uintptr_t socketHandle);
    bool HandlePacketBuffer(uintptr_t socketHandle, std::string &buffer, bool &authenticated);
    bool HandlePacket(uintptr_t socketHandle, int32_t requestId, int32_t type, std::string_view body, bool &authenticated);
    bool IsSourceRconPassword(std::string_view password) const;
    std::string ExecuteCommand(std::string command);
    void JoinConnectionThreads();

    class ActiveClientCommand;

    std::mutex m_mutex;
    std::condition_variable m_clientIdle;
    ClientGC *m_client{};
    size_t m_activeClientCommands{};
    std::thread m_thread;
    std::vector<std::thread> m_connectionThreads;
    std::atomic<bool> m_running{ false };
    uintptr_t m_listenSocket{ UINTPTR_MAX };
};
