#include "stdafx.h"
#include "rcon_server.h"
#include "config.h"
#include "gc_client.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace
{

constexpr int32_t SourceRconResponseValue = 0;
constexpr int32_t SourceRconExecCommand = 2;
constexpr int32_t SourceRconAuth = 3;
constexpr int32_t SourceRconAuthResponse = 2;
constexpr int32_t SourceRconMinPacketSize = 10; // request id + type + two null bytes
constexpr int32_t SourceRconMaxPacketSize = 4096;

#ifdef _WIN32
using SocketType = SOCKET;
constexpr SocketType InvalidSocket = INVALID_SOCKET;
#else
using SocketType = int;
constexpr SocketType InvalidSocket = -1;
#endif

SocketType FromHandle(uintptr_t handle)
{
    return static_cast<SocketType>(handle);
}

uintptr_t ToHandle(SocketType socket)
{
    return static_cast<uintptr_t>(socket);
}

void CloseSocket(SocketType socket)
{
    if (socket == InvalidSocket)
    {
        return;
    }

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void ShutdownSocket(SocketType socket)
{
    if (socket == InvalidSocket)
    {
        return;
    }

#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

int32_t ReadInt32LE(const char *data)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(data);
    return static_cast<int32_t>(
        static_cast<uint32_t>(bytes[0])
        | (static_cast<uint32_t>(bytes[1]) << 8)
        | (static_cast<uint32_t>(bytes[2]) << 16)
        | (static_cast<uint32_t>(bytes[3]) << 24));
}

void AppendInt32LE(std::string &buffer, int32_t value)
{
    uint32_t unsignedValue = static_cast<uint32_t>(value);
    buffer.push_back(static_cast<char>(unsignedValue & 0xff));
    buffer.push_back(static_cast<char>((unsignedValue >> 8) & 0xff));
    buffer.push_back(static_cast<char>((unsignedValue >> 16) & 0xff));
    buffer.push_back(static_cast<char>((unsignedValue >> 24) & 0xff));
}

bool IsValidSourcePacketSize(int32_t size)
{
    return size >= SourceRconMinPacketSize && size <= SourceRconMaxPacketSize;
}

bool SendAll(SocketType socket, const char *data, size_t size)
{
    while (size)
    {
#if defined(_WIN32) || !defined(MSG_NOSIGNAL)
        constexpr int SendFlags = 0;
#else
        constexpr int SendFlags = MSG_NOSIGNAL;
#endif
        int sent = send(socket, data, static_cast<int>(size), SendFlags);
        if (sent <= 0)
        {
            return false;
        }

        data += sent;
        size -= sent;
    }

    return true;
}

bool SendSourcePacket(SocketType socket, int32_t requestId, int32_t type, std::string_view body)
{
    if (body.size() > static_cast<size_t>(SourceRconMaxPacketSize - SourceRconMinPacketSize))
    {
        body = body.substr(0, static_cast<size_t>(SourceRconMaxPacketSize - SourceRconMinPacketSize));
    }

    std::string packet;
    packet.reserve(sizeof(int32_t) * 3 + body.size() + 2);
    AppendInt32LE(packet, static_cast<int32_t>(sizeof(int32_t) * 2 + body.size() + 2));
    AppendInt32LE(packet, requestId);
    AppendInt32LE(packet, type);
    packet.append(body.data(), body.size());
    packet.push_back('\0');
    packet.push_back('\0');
    return SendAll(socket, packet.data(), packet.size());
}

} // namespace

RconServer::RconServer() = default;

RconServer::~RconServer()
{
    Stop();
}

void RconServer::Start()
{
    Platform::Print("RCON: config enabled=%d bind_address=%s port=%u protocol=source password=%s\n",
        GetConfig().RconEnabled() ? 1 : 0,
        GetConfig().RconBindAddress().c_str(),
        GetConfig().RconPort(),
        GetConfig().RconPassword().empty() ? "empty" : "set");

    if (!GetConfig().RconEnabled())
    {
        Platform::Print("RCON: disabled\n");
        return;
    }

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        Platform::Print("RCON: already running\n");
        return;
    }

    Platform::Print("RCON: starting listener thread\n");
    m_thread = std::thread{ &RconServer::ThreadMain, this };
}

void RconServer::Stop()
{
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false))
    {
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        return;
    }

    SocketType listenSocket = InvalidSocket;
    {
        std::lock_guard lock{ m_mutex };
        listenSocket = FromHandle(m_listenSocket);
        m_listenSocket = ToHandle(InvalidSocket);
    }

    ShutdownSocket(listenSocket);
    CloseSocket(listenSocket);

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void RconServer::RegisterClient(ClientGC *client)
{
    Start();

    std::lock_guard lock{ m_mutex };
    m_client = client;
}

void RconServer::UnregisterClient(ClientGC *client)
{
    std::lock_guard lock{ m_mutex };
    if (m_client == client)
    {
        m_client = nullptr;
    }
}

void RconServer::ThreadMain()
{
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        Platform::Print("RCON: WSAStartup failed\n");
        m_running.store(false);
        return;
    }
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *result = nullptr;
    std::string port = std::to_string(GetConfig().RconPort());
    int gai = getaddrinfo(GetConfig().RconBindAddress().c_str(), port.c_str(), &hints, &result);
    if (gai != 0)
    {
        Platform::Print("RCON: getaddrinfo failed for %s:%s\n",
            GetConfig().RconBindAddress().c_str(),
            port.c_str());
        m_running.store(false);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    SocketType listenSocket = InvalidSocket;
    for (addrinfo *it = result; it; it = it->ai_next)
    {
        listenSocket = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (listenSocket == InvalidSocket)
        {
            continue;
        }

        int reuse = 1;
        setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

        if (bind(listenSocket, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0
            && listen(listenSocket, 8) == 0)
        {
            break;
        }

        CloseSocket(listenSocket);
        listenSocket = InvalidSocket;
    }

    freeaddrinfo(result);

    if (listenSocket == InvalidSocket)
    {
        Platform::Print("RCON: failed to listen on %s:%s\n",
            GetConfig().RconBindAddress().c_str(),
            port.c_str());
        m_running.store(false);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    {
        std::lock_guard lock{ m_mutex };
        m_listenSocket = ToHandle(listenSocket);
    }

    Platform::Print("RCON: listening on %s:%s\n", GetConfig().RconBindAddress().c_str(), port.c_str());

    while (m_running.load())
    {
        SocketType clientSocket = accept(listenSocket, nullptr, nullptr);
        if (clientSocket == InvalidSocket)
        {
            if (m_running.load())
            {
                Platform::Print("RCON: accept failed\n");
            }
            break;
        }

        HandleConnection(ToHandle(clientSocket));
    }

    {
        std::lock_guard lock{ m_mutex };
        m_listenSocket = ToHandle(InvalidSocket);
    }

    if (m_running.load())
    {
        CloseSocket(listenSocket);
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void RconServer::HandleConnection(uintptr_t socketHandle)
{
    SocketType socket = FromHandle(socketHandle);

#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
    int noSigPipe = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif

#ifdef _WIN32
    DWORD timeoutMs = 1000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeoutMs), sizeof(timeoutMs));
#else
    timeval timeout{};
    timeout.tv_sec = 1;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

    std::string buffer;
    char chunk[1024];
    bool authenticated = false;

    while (m_running.load())
    {
        int received = recv(socket, chunk, sizeof(chunk), 0);
        if (received == 0)
        {
            break;
        }

        if (received < 0)
        {
#ifndef _WIN32
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
#else
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                continue;
            }
#endif
            break;
        }

        buffer.append(chunk, chunk + received);

        if (!HandlePacketBuffer(socketHandle, buffer, authenticated))
        {
            break;
        }
    }

    CloseSocket(socket);
}

bool RconServer::HandlePacketBuffer(uintptr_t socketHandle, std::string &buffer, bool &authenticated)
{
    while (buffer.size() >= sizeof(int32_t))
    {
        int32_t packetSize = ReadInt32LE(buffer.data());
        if (!IsValidSourcePacketSize(packetSize))
        {
            Platform::Print("RCON: malformed Source packet size %d\n", packetSize);
            return false;
        }

        size_t fullPacketSize = sizeof(int32_t) + static_cast<size_t>(packetSize);
        if (buffer.size() < fullPacketSize)
        {
            return true;
        }

        const char *packet = buffer.data() + sizeof(int32_t);
        int32_t requestId = ReadInt32LE(packet);
        int32_t type = ReadInt32LE(packet + sizeof(int32_t));
        std::string_view payload{ packet + sizeof(int32_t) * 2, static_cast<size_t>(packetSize - sizeof(int32_t) * 2) };

        size_t firstTerminator = payload.find('\0');
        if (firstTerminator == std::string_view::npos)
        {
            Platform::Print("RCON: malformed Source packet without body terminator\n");
            return false;
        }

        std::string_view body = payload.substr(0, firstTerminator);
        std::string_view tail = payload.substr(firstTerminator + 1);
        if (tail.empty() || tail.find('\0') == std::string_view::npos)
        {
            Platform::Print("RCON: malformed Source packet without empty terminator\n");
            return false;
        }

        if (!HandlePacket(socketHandle, requestId, type, body, authenticated))
        {
            return false;
        }

        buffer.erase(0, fullPacketSize);
    }

    return true;
}

bool RconServer::HandlePacket(uintptr_t socketHandle, int32_t requestId, int32_t type, std::string_view body, bool &authenticated)
{
    SocketType socket = FromHandle(socketHandle);

    if (type == SourceRconAuth)
    {
        authenticated = IsSourceRconPassword(body);
        int32_t authResponseId = authenticated ? requestId : -1;

        if (!authenticated)
        {
            Platform::Print("RCON: Source auth failed\n");
        }

        return SendSourcePacket(socket, requestId, SourceRconResponseValue, {})
            && SendSourcePacket(socket, authResponseId, SourceRconAuthResponse, {});
    }

    if (type != SourceRconExecCommand)
    {
        std::string response = "ERR unsupported packet type";
        return SendSourcePacket(socket, requestId, SourceRconResponseValue, response);
    }

    if (!authenticated)
    {
        std::string response = "ERR not authenticated";
        return SendSourcePacket(socket, requestId, SourceRconResponseValue, response);
    }

    std::string command{ body };
    std::string response = ExecuteCommand(std::move(command));
    return SendSourcePacket(socket, requestId, SourceRconResponseValue, response);
}

bool RconServer::IsSourceRconPassword(std::string_view password) const
{
    const std::string &configuredPassword = GetConfig().RconPassword();
    return configuredPassword.empty() || password == configuredPassword;
}

std::string RconServer::ExecuteCommand(std::string command)
{
    std::istringstream stream{ command };
    std::string name;
    stream >> name;

    for (char &ch : name)
    {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }

    if (name == "ping")
    {
        return "OK pong";
    }

    std::lock_guard lock{ m_mutex };
    ClientGC *client = m_client;

    if (!client)
    {
        if (name == "status")
        {
            return "OK rcon=enabled client=none";
        }

        if (name == "clients")
        {
            return "OK no clients";
        }

        if (name == "help")
        {
            return "OK commands: help, ping, status, clients, give_item <defindex> [count], remove_item <itemid>, refresh_inventory";
        }

        return "ERR no client gc";
    }

    return client->RunRconCommand(std::move(command));
}
