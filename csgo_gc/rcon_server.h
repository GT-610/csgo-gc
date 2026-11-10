#pragma once

#include <functional>

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

    using ServerCommandHandler = std::function<std::optional<std::string>(std::string_view)>;
    void SetServerCommandHandler(ServerCommandHandler handler);

private:
    friend struct RconServerTestAccess;

    enum class ListenerState
    {
        NotStarted,
        Starting,
        Running,
        Failed,
        Stopped
    };

    void ThreadMain();
    void MarkListenerFailed();
    void HandleConnection(uintptr_t socketHandle);
    bool HandlePacketBuffer(uintptr_t socketHandle, std::string &buffer, bool &authenticated);
    bool HandlePacket(uintptr_t socketHandle, int32_t requestId, int32_t type, std::string_view body, bool &authenticated);
    bool IsSourceRconPassword(std::string_view password) const;
    std::string ExecuteCommand(std::string command);
    void JoinConnectionThreads();

    class ActiveClientCommand;

    std::mutex m_lifecycleMutex;
    std::mutex m_mutex;
    std::condition_variable m_clientIdle;
    ClientGC *m_client{};
    ServerCommandHandler m_serverCommandHandler;
    size_t m_activeClientCommands{};
    std::thread m_thread;
    std::vector<std::thread> m_connectionThreads;
    std::atomic<bool> m_running{ false };
    std::atomic<ListenerState> m_listenerState{ ListenerState::NotStarted };
    uintptr_t m_listenSocket{ UINTPTR_MAX };
};
