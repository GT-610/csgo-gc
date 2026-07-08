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

std::string Trim(std::string_view value)
{
    while (!value.empty() && value.front() <= ' ')
    {
        value.remove_prefix(1);
    }

    while (!value.empty() && value.back() <= ' ')
    {
        value.remove_suffix(1);
    }

    return std::string{ value };
}

bool SendAll(SocketType socket, const char *data, size_t size)
{
    while (size)
    {
        int sent = send(socket, data, static_cast<int>(size), 0);
        if (sent <= 0)
        {
            return false;
        }

        data += sent;
        size -= sent;
    }

    return true;
}

} // namespace

RconServer::RconServer() = default;

RconServer::~RconServer()
{
    Stop();
}

void RconServer::Start()
{
    if (!GetConfig().RconEnabled())
    {
        return;
    }

    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true))
    {
        return;
    }

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

    CloseSocket(listenSocket);

    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void RconServer::RegisterClient(ClientGC *client)
{
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

        while (true)
        {
            size_t newline = buffer.find('\n');
            if (newline == std::string::npos)
            {
                break;
            }

            std::string line = Trim(std::string_view{ buffer.data(), newline });
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            buffer.erase(0, newline + 1);

            if (line.empty())
            {
                continue;
            }

            std::string response = ExecuteCommand(std::move(line));
            response.push_back('\n');
            if (!SendAll(socket, response.data(), response.size()))
            {
                CloseSocket(socket);
                return;
            }
        }
    }

    CloseSocket(socket);
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
