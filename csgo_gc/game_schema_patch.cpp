#include "stdafx.h"
#include "game_schema_patch.h"

static bool ReadWholeFile(const char *path, std::string &out)
{
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        return false;
    }

    out.clear();
    char buffer[1 << 16];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        out.append(buffer, read);
    }

    bool ok = !ferror(f);
    fclose(f);
    return ok;
}

static bool WriteWholeFile(const char *path, const std::string &data)
{
    // write to a temporary file first so a failed write can't truncate the game file
    std::string temporaryPath = std::string{ path } + ".tmp";
    FILE *f = fopen(temporaryPath.c_str(), "wb");
    if (!f)
    {
        return false;
    }

    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok &= fclose(f) == 0;
    if (!ok)
    {
        remove(temporaryPath.c_str());
        return false;
    }

    remove(path);
    if (rename(temporaryPath.c_str(), path) != 0)
    {
        remove(temporaryPath.c_str());
        return false;
    }

    return true;
}

static bool FileExists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
    {
        fclose(f);
        return true;
    }
    return false;
}

const char *GameSchemaPatch::OriginalSchemaPath()
{
    return FileExists(BackupPath) ? BackupPath : SchemaPath;
}

size_t GameSchemaPatch::PatchLootLists(std::string &text, const LootListPreviews &previews)
{
    struct Range
    {
        size_t begin; // just after the list's '{'
        size_t end; // at the list's '}'
        const std::vector<std::string> *entries;
    };

    std::vector<Range> ranges;
    std::vector<std::string> stack; // names of the open blocks
    std::string lastString;
    bool haveLastString = false;

    // the list currently being replaced, if any
    size_t openRangeDepth = 0;
    Range openRange{};
    bool inRange = false;

    for (size_t i = 0; i < text.size(); i++)
    {
        char c = text[i];

        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
        {
            while (i < text.size() && text[i] != '\n')
            {
                i++;
            }
            continue;
        }

        if (c == '"')
        {
            size_t start = ++i;
            while (i < text.size() && text[i] != '"')
            {
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    i++;
                }
                i++;
            }
            lastString.assign(text, start, i - start);
            haveLastString = true;
            continue;
        }

        if (c == '{')
        {
            stack.push_back(haveLastString ? lastString : std::string{});
            haveLastString = false;

            if (!inRange && stack.size() == 3 && stack[0] == "items_game" && stack[1] == "client_loot_lists")
            {
                auto it = previews.find(stack[2]);
                if (it != previews.end())
                {
                    inRange = true;
                    openRangeDepth = stack.size();
                    openRange = Range{ i + 1, 0, &it->second };
                }
            }
            continue;
        }

        if (c == '}')
        {
            if (inRange && stack.size() == openRangeDepth)
            {
                openRange.end = i;
                ranges.push_back(openRange);
                inRange = false;
            }

            if (!stack.empty())
            {
                stack.pop_back();
            }
            haveLastString = false;
            continue;
        }
    }

    // replace back to front so earlier offsets stay valid
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it)
    {
        std::string body = "\n";
        for (const std::string &entry : *it->entries)
        {
            body += "\t\t\t\"" + entry + "\"\t\t\"1\"\n";
        }
        body += "\t\t";
        text.replace(it->begin, it->end - it->begin, body);
    }

    return ranges.size();
}

bool GameSchemaPatch::Apply(const LootListPreviews &previews)
{
    std::string original;
    if (!ReadWholeFile(OriginalSchemaPath(), original))
    {
        Platform::Print("custom_cases: could not read %s, case previews not updated\n", SchemaPath);
        return false;
    }

    std::string patched = original;
    size_t replaced = PatchLootLists(patched, previews);
    if (replaced != previews.size())
    {
        Platform::Print("custom_cases: found %zu of %zu case contents lists to update\n", replaced, previews.size());
    }

    std::string current;
    if (ReadWholeFile(SchemaPath, current) && current == patched)
    {
        return false; // already up to date
    }

    if (!FileExists(BackupPath) && !WriteWholeFile(BackupPath, original))
    {
        Platform::Print("custom_cases: could not back up %s, case previews not updated\n", SchemaPath);
        return false;
    }

    if (!WriteWholeFile(SchemaPath, patched))
    {
        Platform::Print("custom_cases: could not write %s, case previews not updated\n", SchemaPath);
        return false;
    }

    Platform::Print("custom_cases: updated %zu case preview(s) in %s, restart the game to see them\n",
        replaced, SchemaPath);
    return true;
}

bool GameSchemaPatch::Restore()
{
    if (!FileExists(BackupPath))
    {
        return false;
    }

    std::string original;
    if (!ReadWholeFile(BackupPath, original) || !WriteWholeFile(SchemaPath, original))
    {
        Platform::Print("custom_cases: could not restore %s from %s\n", SchemaPath, BackupPath);
        return false;
    }

    remove(BackupPath);
    Platform::Print("custom_cases: restored the original %s, restart the game to see it\n", SchemaPath);
    return true;
}
