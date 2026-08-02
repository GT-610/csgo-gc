#include "stdafx.h"
#include "keyvalue.h"
#include <cerrno>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

constexpr auto SubkeyReserveCount = 8;

// for writing binary keyvalues
enum class BinaryCommand : uint8_t
{
    Subkey,
    String,
    Int,
    Float,
    Ptr,
    Wstring,
    Color,
    Uint64,
    CompiledIntByte,
    CompiledInt0,
    CompiledInt1,
    Terminate
};

class KeyValueParser
{
public:
    KeyValueParser(std::string_view str)
        : m_ptr{ str.begin() }
        , m_end{ str.end() }
        , m_lineNumber{ 1 }
    {
    }

    // skips whitespace, returns false on eof
    [[nodiscard]] bool NextToken()
    {
    start:
        while (true)
        {
            if (IsEndOfFile())
            {
                return false;
            }

            if (*m_ptr > ' ')
            {
                break;
            }

            if (*m_ptr == '\n')
            {
                m_lineNumber++;
            }

            m_ptr++;
        }

        if (m_ptr[0] != '/' || m_end - m_ptr < 2 || m_ptr[1] != '/')
        {
            return true;
        }

        m_ptr += 2;

        while (!IsEndOfFile() && *m_ptr != '\n')
        {
            m_ptr++;
        }

        if (IsEndOfFile())
        {
            return false;
        }

        goto start;
    }

    bool ParseString(std::string_view &string)
    {
        m_ptr++; // skip the start quote

        auto start = m_ptr;

        while (!IsEndOfFile() && *m_ptr != '"')
        {
            m_ptr++;
        }

        if (IsEndOfFile())
        {
            return false;
        }

        size_t length = m_ptr - start;
        m_ptr++; // skip the end quote
        string = { &start[0], length };

        return true;
    }

    char PeekCharacter() const
    {
        assert(!IsEndOfFile());
        return m_ptr[0];
    }

    void SkipCharacter()
    {
        assert(!IsEndOfFile());
        m_ptr++;
    }

private:
    bool IsEndOfFile() const { return m_ptr >= m_end; }

    std::string_view::const_iterator m_ptr;
    std::string_view::const_iterator m_end;

    // for error reports
    int m_lineNumber;
};

std::string LoadFile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return {};
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string buffer;
    buffer.resize(size);
    long bytesRead = static_cast<long>(fread(buffer.data(), 1, size, f));

    fclose(f);

    if (bytesRead != size)
    {
        return {};
    }

    return buffer;
}

static KeyValueFileResult LoadKeyValueFile(const char *path, std::string &buffer)
{
    errno = 0;
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return errno == ENOENT ? KeyValueFileResult::NotFound : KeyValueFileResult::ReadError;
    }

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return KeyValueFileResult::ReadError;
    }

    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return KeyValueFileResult::ReadError;
    }

    if (size == 0)
    {
        fclose(f);
        return KeyValueFileResult::Empty;
    }

    buffer.resize(static_cast<size_t>(size));
    size_t bytesRead = fread(buffer.data(), 1, buffer.size(), f);
    bool readFailed = bytesRead != buffer.size() || ferror(f);
    readFailed |= fclose(f) != 0;

    return readFailed ? KeyValueFileResult::ReadError : KeyValueFileResult::Success;
}

KeyValue::KeyValue(std::string_view name)
    : m_name{ name }
{
}

bool KeyValue::ParseFromFile(const char *path)
{
    return ParseFromFileDetailed(path) == KeyValueFileResult::Success;
}

KeyValueFileResult KeyValue::ParseFromFileDetailed(const char *path)
{
    std::string data;
    KeyValueFileResult result = LoadKeyValueFile(path, data);
    if (result != KeyValueFileResult::Success)
    {
        return result;
    }

    std::string_view dataView{ data };
    constexpr std::string_view Utf8Bom{ "\xef\xbb\xbf", 3 };
    if (dataView.starts_with(Utf8Bom))
    {
        dataView.remove_prefix(Utf8Bom.size());
    }

    if (dataView.empty())
    {
        return KeyValueFileResult::Empty;
    }

    m_subkeys.clear();
    m_string.clear();

    KeyValueParser parser{ dataView };
    return Parse(parser) ? KeyValueFileResult::Success : KeyValueFileResult::InvalidFormat;
}

bool KeyValue::WriteToFile(const char *path)
{
    std::string temporaryPath = path;
    temporaryPath.append(".tmp");

    FILE *f = fopen(temporaryPath.c_str(), "wb");
    if (!f)
    {
        return false;
    }

    WriteToFile(f, 0);

    bool writeSucceeded = !ferror(f) && fflush(f) == 0;
    writeSucceeded &= fclose(f) == 0;
    if (!writeSucceeded)
    {
        remove(temporaryPath.c_str());
        return false;
    }

#if defined(_WIN32)
    bool replaced = MoveFileExA(temporaryPath.c_str(), path,
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    bool replaced = rename(temporaryPath.c_str(), path) == 0;
#endif

    if (!replaced)
    {
        remove(temporaryPath.c_str());
    }

    return replaced;
}

static void BinaryWriteCommand(std::string &buffer, BinaryCommand cmd)
{
    buffer.append(1, static_cast<char>(cmd));
}

static void BinaryWriteString(std::string &buffer, std::string_view string)
{
    buffer.append(string);
    buffer.append(1, '\0');
}

void KeyValue::BinaryWriteToString(std::string &buffer)
{
    for (KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_string.empty() && subkey.m_subkeys.empty())
        {
            continue;
        }

        if (subkey.m_string.size())
        {
            BinaryWriteCommand(buffer, BinaryCommand::String);
            BinaryWriteString(buffer, subkey.m_name);
            BinaryWriteString(buffer, subkey.m_string);
        }
        else
        {
            BinaryWriteCommand(buffer, BinaryCommand::Subkey);
            BinaryWriteString(buffer, subkey.m_name);
            subkey.BinaryWriteToString(buffer);
        }
    }

    BinaryWriteCommand(buffer, BinaryCommand::Terminate);
}

bool KeyValue::Parse(KeyValueParser &parser, bool expectClosingBrace)
{
    m_subkeys.reserve(SubkeyReserveCount);

    while (true)
    {
        if (!parser.NextToken())
        {
            return !expectClosingBrace;
        }

        KeyValue *current;

        switch (parser.PeekCharacter())
        {
        case '"':
            {
                std::string_view name;
                if (!parser.ParseString(name))
                {
                    return false;
                }
                current = FindOrCreateSubkey(name);
            }
            break;

        case '}':
            parser.SkipCharacter();
            return expectClosingBrace;

        default:
            return false;
        }

        if (!parser.NextToken())
        {
            return false;
        }

        switch (parser.PeekCharacter())
        {
        case '"':
            {
                std::string_view value;
                if (!parser.ParseString(value))
                {
                    return false;
                }
                current->m_string = value;
            }
            break;

        case '{':
            parser.SkipCharacter();
            if (!current->Parse(parser, true))
            {
                return false;
            }
            break;

        default:
            return false;
        }
    }
}

KeyValue *KeyValue::FindOrCreateSubkey(std::string_view name)
{
    for (KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_name == name)
        {
            return &subkey;
        }
    }

    return &m_subkeys.emplace_back(name);
}

void KeyValue::WriteToFile(FILE *f, int indent)
{
    if (indent)
    {
        fputs("\n", f);

        for (int i = 0; i < indent - 1; i++)
        {
            fputs("\t", f);
        }

        fputs("{\n", f);
    }

    for (KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_string.empty() && subkey.m_subkeys.empty())
        {
            continue;
        }

        for (int i = 0; i < indent; i++)
        {
            fputs("\t", f);
        }

        if (subkey.m_string.size())
        {
            fprintf(f, "\"%s\"\t\t\"%s\"\n", subkey.m_name.c_str(), subkey.m_string.c_str());
        }
        else
        {
            fprintf(f, "\"%s\"", subkey.m_name.c_str());
            subkey.WriteToFile(f, indent + 1);
        }
    }

    if (indent)
    {
        for (int i = 0; i < indent - 1; i++)
        {
            fputs("	", f);
        }

        fputs("}\n", f);
    }
}

const KeyValue *KeyValue::GetSubkey(std::string_view name) const
{
    for (const KeyValue &subkey : m_subkeys)
    {
        if (subkey.m_name == name)
        {
            return &subkey;
        }
    }

    return nullptr;
}

std::string_view KeyValue::GetString(std::string_view name, std::string_view fallback) const
{
    const KeyValue *subkey = GetSubkey(name);
    if (!subkey)
    {
        return fallback;
    }

    return subkey->m_string;
}

KeyValue &KeyValue::AddSubkey(std::string_view name)
{
    return m_subkeys.emplace_back(name);
}

void KeyValue::AddString(std::string_view name, std::string_view value)
{
    KeyValue &subkey = AddSubkey(name);
    subkey.m_string = value;
}
