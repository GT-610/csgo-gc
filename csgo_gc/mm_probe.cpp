#include "stdafx.h"
#include "mm_probe.h"
#include "config.h"
#include "gc_shared.h"

#include <cstdio>
#include <ctime>

namespace MMProbe
{

namespace
{

std::mutex s_logMutex;

void AppendHex(std::string &out, const uint8_t *data, size_t size, size_t limit)
{
    static const char Digits[] = "0123456789abcdef";
    size_t count = size < limit ? size : limit;
    for (size_t i = 0; i < count; i++)
    {
        out.push_back(Digits[data[i] >> 4]);
        out.push_back(Digits[data[i] & 0xf]);
    }
    if (count < size)
    {
        out += "...(";
        out += std::to_string(size);
        out += " bytes)";
    }
}

bool ReadVarint(const uint8_t *data, size_t size, size_t &offset, uint64_t &value)
{
    value = 0;
    for (int shift = 0; shift < 64; shift += 7)
    {
        if (offset >= size)
        {
            return false;
        }
        uint8_t byte = data[offset++];
        value |= uint64_t(byte & 0x7f) << shift;
        if (!(byte & 0x80))
        {
            return true;
        }
    }
    return false;
}

bool IsPrintable(const uint8_t *data, size_t size)
{
    if (size == 0)
    {
        return false;
    }
    for (size_t i = 0; i < size; i++)
    {
        if (data[i] < 0x20 || data[i] > 0x7e)
        {
            return false;
        }
    }
    return true;
}

void Indent(std::string &out, int depth)
{
    out.append(static_cast<size_t>(depth) * 2 + 2, ' ');
}

// Split "a,b,c" into unsigned numbers; "self" maps to selfAccountId.
bool ParseNumberList(std::string_view text, uint32_t selfAccountId, std::vector<uint64_t> &values)
{
    while (!text.empty())
    {
        size_t comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        if (item == "self")
        {
            values.push_back(selfAccountId);
        }
        else
        {
            uint64_t value;
            auto result = std::from_chars(item.data(), item.data() + item.size(), value);
            if (item.empty() || result.ec != std::errc{} || result.ptr != item.data() + item.size())
            {
                return false;
            }
            values.push_back(value);
        }
        if (comma == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return !values.empty();
}

template<typename T>
bool ParseNumber(std::string_view text, uint32_t selfAccountId, T &value)
{
    if (text == "self")
    {
        value = static_cast<T>(selfAccountId);
        return true;
    }
    // accept 0x prefixed hex for 64-bit ids copied from a capture
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text.remove_prefix(2);
        base = 16;
    }
    T parsed{};
    auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, base);
    if (text.empty() || result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }
    value = parsed;
    return true;
}

struct KeyValueArg
{
    std::string key;
    std::string_view value;
};

std::string SplitArgs(const std::vector<std::string> &args, std::vector<KeyValueArg> &parsed)
{
    for (const std::string &arg : args)
    {
        size_t separator = arg.find('=');
        if (separator == std::string::npos || separator == 0)
        {
            return "ERR expected key=value, got " + arg;
        }
        KeyValueArg kv;
        kv.key = arg.substr(0, separator);
        kv.value = std::string_view{ arg }.substr(separator + 1);
        parsed.push_back(std::move(kv));
    }
    return {};
}

std::string Invalid(const std::string &key)
{
    return "ERR invalid value for " + key;
}

std::string Unknown(const std::string &key)
{
    return "ERR unknown key " + key;
}

} // namespace

bool IsMatchmakingMessage(uint32_t type)
{
    type &= ~ProtobufMask;
    return type >= FirstMatchmakingMessage && type <= LastMatchmakingMessage;
}

bool ParseIPv4(std::string_view text, uint32_t &ip)
{
    uint32_t result = 0;
    for (int part = 0; part < 4; part++)
    {
        size_t dot = text.find('.');
        if ((part < 3) != (dot != std::string_view::npos))
        {
            return false;
        }
        std::string_view octet = text.substr(0, dot);
        uint32_t value;
        auto parsed = std::from_chars(octet.data(), octet.data() + octet.size(), value);
        if (octet.empty() || octet.size() > 3 || parsed.ec != std::errc{}
            || parsed.ptr != octet.data() + octet.size() || value > 255)
        {
            return false;
        }
        result = (result << 8) | value;
        if (dot != std::string_view::npos)
        {
            text.remove_prefix(dot + 1);
        }
    }
    ip = result;
    return true;
}

bool DecodeProtobuf(const uint8_t *data, size_t size, int depth, std::string &out)
{
    size_t offset = 0;
    while (offset < size)
    {
        uint64_t key;
        if (!ReadVarint(data, size, offset, key))
        {
            return false;
        }
        uint64_t field = key >> 3;
        uint32_t wireType = key & 7;
        if (field == 0)
        {
            return false;
        }

        std::string line;
        Indent(line, depth);
        line += std::to_string(field);
        line += ": ";

        switch (wireType)
        {
        case 0:
        {
            uint64_t value;
            if (!ReadVarint(data, size, offset, value))
            {
                return false;
            }
            line += std::to_string(value);
            if (value > 0xffffffffull)
            {
                char hex[32];
                snprintf(hex, sizeof(hex), " (0x%llx)", static_cast<unsigned long long>(value));
                line += hex;
            }
            else if (value >= 0x01000000 && value <= 0xffffffff)
            {
                // may be a host-order IPv4 address, show it to save the tester a conversion
                char ip[48];
                snprintf(ip, sizeof(ip), " (ip? %u.%u.%u.%u)",
                    static_cast<unsigned>((value >> 24) & 0xff), static_cast<unsigned>((value >> 16) & 0xff),
                    static_cast<unsigned>((value >> 8) & 0xff), static_cast<unsigned>(value & 0xff));
                line += ip;
            }
            out += line;
            out += '\n';
            break;
        }
        case 1:
        {
            if (size - offset < 8)
            {
                return false;
            }
            uint64_t value;
            memcpy(&value, data + offset, 8);
            offset += 8;
            char text[64];
            snprintf(text, sizeof(text), "fixed64 %llu (0x%llx)",
                static_cast<unsigned long long>(value), static_cast<unsigned long long>(value));
            out += line + text + '\n';
            break;
        }
        case 5:
        {
            if (size - offset < 4)
            {
                return false;
            }
            uint32_t value;
            memcpy(&value, data + offset, 4);
            offset += 4;
            float asFloat;
            memcpy(&asFloat, &value, 4);
            char text[80];
            snprintf(text, sizeof(text), "fixed32 %u (0x%x, float %g)", value, value, asFloat);
            out += line + text + '\n';
            break;
        }
        case 2:
        {
            uint64_t length;
            if (!ReadVarint(data, size, offset, length) || length > size - offset)
            {
                return false;
            }
            const uint8_t *payload = data + offset;
            size_t payloadSize = static_cast<size_t>(length);
            offset += payloadSize;

            if (IsPrintable(payload, payloadSize))
            {
                line += '"';
                line.append(reinterpret_cast<const char *>(payload), payloadSize);
                line += '"';
                out += line + '\n';
                break;
            }

            std::string nested;
            if (payloadSize && depth < 6 && DecodeProtobuf(payload, payloadSize, depth + 1, nested))
            {
                out += line + "{\n" + nested;
                Indent(out, depth);
                out += "}\n";
                break;
            }

            line += "bytes ";
            AppendHex(line, payload, payloadSize, 64);
            out += line + '\n';
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

std::string FormatMessage(bool server, bool outbound, uint32_t type, const void *data, uint32_t size)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t unmasked = type & ~ProtobufMask;

    std::string out;
    char head[160];
    snprintf(head, sizeof(head), "%s %s %u %s size=%u\n",
        server ? "server" : "client",
        outbound ? "game->gc" : "gc->game",
        unmasked, MessageName(unmasked), size);
    out += head;

    // Every buffer at this boundary starts with the masked type, then for
    // protobuf messages a header length and a CMsgProtoBufHeader.
    size_t bodyOffset = 0;
    bool framed = false;
    if (size >= 8 && (type & ProtobufMask))
    {
        uint32_t headerSize;
        memcpy(&headerSize, bytes + 4, 4);
        if (headerSize <= size - 8)
        {
            bodyOffset = 8 + headerSize;
            framed = true;
            if (headerSize)
            {
                out += "  header:\n";
                std::string header;
                DecodeProtobuf(bytes + 8, headerSize, 1, header);
                out += header;
            }
        }
    }

    if (framed)
    {
        out += "  body:\n";
        std::string body;
        if (!DecodeProtobuf(bytes + bodyOffset, size - bodyOffset, 1, body))
        {
            body += "  (body is not a well-formed protobuf message)\n";
        }
        out += body;
    }

    out += "  hex: ";
    AppendHex(out, bytes, size, 4096);
    out += '\n';
    return out;
}

void Observe(bool server, bool outbound, uint32_t type, const void *data, uint32_t size)
{
    if (!data || !IsMatchmakingMessage(type) || !GetConfig().m_mmProbe)
    {
        return;
    }

    std::string text = FormatMessage(server, outbound, type, data, size);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    {
        std::lock_guard lock{ s_logMutex };
        FILE *f = fopen(LogPath, "a");
        if (f)
        {
            fprintf(f, "[%lld] %s", static_cast<long long>(ms), text.c_str());
            fclose(f);
        }
    }

    Platform::Print("mm_probe: %s %s %u %s (%u bytes, see %s)\n",
        server ? "server" : "client",
        outbound ? "game->gc" : "gc->game",
        type & ~ProtobufMask, MessageName(type & ~ProtobufMask), size, LogPath);
}

std::string BuildReserve(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientReserve &message)
{
    std::vector<KeyValueArg> parsed;
    std::string error = SplitArgs(args, parsed);
    if (!error.empty())
    {
        return error;
    }

    for (const KeyValueArg &kv : parsed)
    {
        const std::string &key = kv.key;
        // created lazily so "not named" keeps meaning "not sent" on the wire
        auto reservation = [&message] { return message.mutable_reservation(); };
        uint64_t u64;
        uint32_t u32;

        if (key == "serverid")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            message.set_serverid(u64);
        }
        else if (key == "ip")
        {
            if (!ParseIPv4(kv.value, u32) && !ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            message.set_direct_udp_ip(u32);
        }
        else if (key == "port")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32) || u32 > 65535) return Invalid(key);
            message.set_direct_udp_port(u32);
        }
        else if (key == "reservationid")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            message.set_reservationid(u64);
        }
        else if (key == "map")
        {
            message.set_map(std::string{ kv.value });
        }
        else if (key == "server_address")
        {
            message.set_server_address(std::string{ kv.value });
        }
        else if (key == "accounts")
        {
            std::vector<uint64_t> ids;
            if (!ParseNumberList(kv.value, selfAccountId, ids)) return Invalid(key);
            for (uint64_t id : ids)
            {
                reservation()->add_account_ids(static_cast<uint32_t>(id));
            }
        }
        else if (key == "party_ids")
        {
            std::vector<uint64_t> ids;
            if (!ParseNumberList(kv.value, selfAccountId, ids)) return Invalid(key);
            for (uint64_t id : ids)
            {
                reservation()->add_party_ids(static_cast<uint32_t>(id));
            }
        }
        else if (key == "game_type")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            reservation()->set_game_type(u32);
        }
        else if (key == "match_id")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            reservation()->set_match_id(u64);
        }
        else if (key == "server_version")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            reservation()->set_server_version(u32);
        }
        else if (key == "flags")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            reservation()->set_flags(u32);
        }
        else if (key == "encryption_key")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            reservation()->set_encryption_key(u64);
        }
        else if (key == "encryption_key_pub")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            reservation()->set_encryption_key_pub(u64);
        }
        else if (key == "tv_master_steamid")
        {
            if (!ParseNumber(kv.value, selfAccountId, u64)) return Invalid(key);
            reservation()->set_tv_master_steamid(u64);
        }
        else
        {
            return Unknown(key);
        }
    }

    return {};
}

std::string BuildUpdate(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate &message)
{
    std::vector<KeyValueArg> parsed;
    std::string error = SplitArgs(args, parsed);
    if (!error.empty())
    {
        return error;
    }

    for (const KeyValueArg &kv : parsed)
    {
        const std::string &key = kv.key;
        int32_t i32;
        std::vector<uint64_t> ids;

        if (key == "matchmaking")
        {
            if (!ParseNumber(kv.value, selfAccountId, i32)) return Invalid(key);
            message.set_matchmaking(i32);
        }
        else if (key == "error")
        {
            message.set_error(std::string{ kv.value });
        }
        else if (key == "waiting" || key == "ongoing" || key == "failping" || key == "penalty"
            || key == "failready" || key == "vacbanned")
        {
            if (!ParseNumberList(kv.value, selfAccountId, ids)) return Invalid(key);
            for (uint64_t id : ids)
            {
                uint32_t accountId = static_cast<uint32_t>(id);
                if (key == "waiting") message.add_waiting_account_id_sessions(accountId);
                else if (key == "ongoing") message.add_ongoingmatch_account_id_sessions(accountId);
                else if (key == "failping") message.add_failping_account_id_sessions(accountId);
                else if (key == "penalty") message.add_penalty_account_id_sessions(accountId);
                else if (key == "failready") message.add_failready_account_id_sessions(accountId);
                else message.add_vacbanned_account_id_sessions(accountId);
            }
        }
        else
        {
            return Unknown(key);
        }
    }

    return {};
}

std::string BuildAbandon(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientAbandon &message)
{
    std::vector<KeyValueArg> parsed;
    std::string error = SplitArgs(args, parsed);
    if (!error.empty())
    {
        return error;
    }

    for (const KeyValueArg &kv : parsed)
    {
        const std::string &key = kv.key;
        uint32_t u32;
        if (key == "account_id")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            message.set_account_id(u32);
        }
        else if (key == "penalty_seconds")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            message.set_penalty_seconds(u32);
        }
        else if (key == "penalty_reason")
        {
            if (!ParseNumber(kv.value, selfAccountId, u32)) return Invalid(key);
            message.set_penalty_reason(u32);
        }
        else
        {
            return Unknown(key);
        }
    }

    return {};
}

} // namespace MMProbe
