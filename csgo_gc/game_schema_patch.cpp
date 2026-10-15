#include "stdafx.h"
#include "game_schema_patch.h"

#include <cerrno>
#include <filesystem>
#include <regex>

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

int GameSchemaPatch::ReplaceRedeemables(std::string &text, uint32_t season, const std::string &blocksText)
{
    struct Range
    {
        size_t begin;
        size_t end;
    };

    const std::string seasonName = std::to_string(season);
    std::vector<Range> removals;
    std::vector<std::string> stack;
    std::vector<size_t> keyStarts; // where each open block's name string begins
    std::string lastString;
    size_t lastStringStart = 0;
    bool haveLastString = false;
    size_t insertAt = std::string::npos;

    for (size_t i = 0; i < text.size(); i++)
    {
        const char c = text[i];

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
            const size_t quote = i;
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
            lastStringStart = quote;
            haveLastString = true;
            continue;
        }

        if (c == '{')
        {
            stack.push_back(haveLastString ? lastString : std::string{});
            keyStarts.push_back(haveLastString ? lastStringStart : i);
            haveLastString = false;
            continue;
        }

        if (c == '}')
        {
            if (stack.size() == 4 && stack[0] == "items_game" && stack[1] == "seasonaloperations"
                && stack[2] == seasonName && stack[3] == "operational_point_redeemable")
            {
                // from the start of the key's line to the end of the closing brace's line
                size_t begin = keyStarts.back();
                while (begin > 0 && (text[begin - 1] == '\t' || text[begin - 1] == ' '))
                {
                    begin--;
                }
                size_t end = i + 1;
                while (end < text.size() && (text[end] == '\r' || text[end] == '\n'))
                {
                    end++;
                    if (text[end - 1] == '\n')
                    {
                        break;
                    }
                }
                removals.push_back({ begin, end });
            }
            else if (stack.size() == 3 && stack[0] == "items_game" && stack[1] == "seasonaloperations"
                && stack[2] == seasonName)
            {
                // just before the closing brace's indentation
                insertAt = i;
                while (insertAt > 0 && (text[insertAt - 1] == '\t' || text[insertAt - 1] == ' '))
                {
                    insertAt--;
                }
            }

            if (!stack.empty())
            {
                stack.pop_back();
                keyStarts.pop_back();
            }
            haveLastString = false;
            continue;
        }
    }

    if (insertAt == std::string::npos)
    {
        return -1;
    }

    // the insert point is after every removal, so do it first
    text.insert(insertAt, blocksText);
    for (auto it = removals.rbegin(); it != removals.rend(); ++it)
    {
        text.erase(it->begin, it->end - it->begin);
    }

    return static_cast<int>(removals.size());
}

namespace
{
struct BlockRange
{
    size_t keyBegin; // the block's name string
    size_t close; // its closing brace
};

// the first block at exactly this path (e.g. items_game/items/4001)
bool FindBlock(const std::string &text, const std::vector<std::string> &path, BlockRange &out)
{
    std::vector<std::string> stack;
    std::vector<size_t> keyStarts;
    std::string lastString;
    size_t lastStringStart = 0;
    bool haveLastString = false;

    for (size_t i = 0; i < text.size(); i++)
    {
        const char c = text[i];

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
            const size_t quote = i;
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
            lastStringStart = quote;
            haveLastString = true;
            continue;
        }

        if (c == '{')
        {
            stack.push_back(haveLastString ? lastString : std::string{});
            keyStarts.push_back(haveLastString ? lastStringStart : i);
            haveLastString = false;
            continue;
        }

        if (c == '}')
        {
            if (stack == path)
            {
                out = { keyStarts.back(), i };
                return true;
            }

            if (!stack.empty())
            {
                stack.pop_back();
                keyStarts.pop_back();
            }
            haveLastString = false;
            continue;
        }
    }

    return false;
}

// where to put new text so it ends up last inside the block that closes at `close`
size_t InsertionPoint(const std::string &text, size_t close)
{
    size_t at = close;
    while (at > 0 && (text[at - 1] == '\t' || text[at - 1] == ' '))
    {
        at--;
    }
    return at;
}
} // namespace

size_t GameSchemaPatch::AddCases(std::string &text, const std::vector<AddedCasePatch> &cases)
{
    std::string itemsText, revolvingText, listsText;
    size_t added = 0;

    for (const AddedCasePatch &addedCase : cases)
    {
        BlockRange source;
        if (!FindBlock(text, { "items_game", "items", std::to_string(addedCase.basedOn) }, source))
        {
            Platform::Print("custom_cases: case %u isn't in items_game.txt, can't add %s\n", addedCase.basedOn,
                addedCase.name.c_str());
            continue;
        }

        std::string block = text.substr(source.keyBegin, source.close + 1 - source.keyBegin);

        // its own def index, name and series
        const std::string oldKey = "\"" + std::to_string(addedCase.basedOn) + "\"";
        if (block.compare(0, oldKey.size(), oldKey) != 0)
        {
            continue;
        }
        block.replace(0, oldKey.size(), "\"" + std::to_string(addedCase.defIndex) + "\"");
        block = std::regex_replace(block, std::regex{ R"re(("name"\s+)"[^"]*")re" },
            "$1\"" + addedCase.name + "\"", std::regex_constants::format_first_only);
        block = std::regex_replace(block,
            std::regex{ R"re(("set supply crate series"\s*\{[^}]*?"value"\s+)"[^"]*")re" },
            "$1\"" + std::to_string(addedCase.series) + "\"", std::regex_constants::format_first_only);

        itemsText += "\t\t" + block + "\n";
        revolvingText += "\t\t\"" + std::to_string(addedCase.series) + "\"\t\t\"" + addedCase.name + "\"\n";
        listsText += "\t\t\"" + addedCase.name + "\"\n\t\t{\n";
        for (const std::string &entry : addedCase.previewEntries)
        {
            listsText += "\t\t\t\"" + entry + "\"\t\t\"1\"\n";
        }
        listsText += "\t\t}\n";
        added++;
    }

    if (!added)
    {
        return 0;
    }

    // put each piece last in its section, doing the later sections first so the
    // earlier offsets stay valid is not needed: each is looked up again
    struct Piece
    {
        const char *section;
        const std::string *text;
    };
    for (const Piece &piece : { Piece{ "items", &itemsText }, Piece{ "revolving_loot_lists", &revolvingText },
             Piece{ "client_loot_lists", &listsText } })
    {
        BlockRange section;
        if (!FindBlock(text, { "items_game", piece.section }, section))
        {
            Platform::Print("custom_cases: no %s section in items_game.txt, added cases may not work\n", piece.section);
            return 0;
        }
        text.insert(InsertionPoint(text, section.close), *piece.text);
    }

    return added;
}

int GameSchemaPatch::ActivateOperation(std::string &text, int64_t now)
{
    // the schedule is start + length + slack, twelve weeks in the real file
    constexpr int64_t RunningSeconds = 12 * 7 * 24 * 3600;

    std::vector<std::string> stack;
    std::string lastString;
    size_t lastStringStart = 0;
    size_t keyStart = 0, valueStart = 0, valueEnd = 0;
    bool haveLastString = false;
    bool pendingKey = false; // "start" was just read, its value is next
    bool found = false;

    for (size_t i = 0; i < text.size() && !found; i++)
    {
        const char c = text[i];

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
            const size_t quote = i;
            size_t start = ++i;
            while (i < text.size() && text[i] != '"')
            {
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    i++;
                }
                i++;
            }

            const std::string value(text, start, i - start);
            if (pendingKey)
            {
                valueStart = start;
                valueEnd = i;
                found = true;
            }
            else if (stack.size() == 2 && stack[0] == "items_game" && stack[1] == "quest_schedule"
                && value == "start")
            {
                pendingKey = true;
            }

            lastString = value;
            lastStringStart = quote;
            haveLastString = true;
            continue;
        }

        if (c == '{')
        {
            stack.push_back(haveLastString ? lastString : std::string{});
            haveLastString = false;
            continue;
        }

        if (c == '}')
        {
            if (!stack.empty())
            {
                stack.pop_back();
            }
            haveLastString = false;
            continue;
        }
    }
    (void)keyStart;
    (void)lastStringStart;

    if (!found)
    {
        return -1;
    }

    int64_t start = 0;
    const std::string current(text, valueStart, valueEnd - valueStart);
    for (char digit : current)
    {
        if (digit < '0' || digit > '9')
        {
            return -1;
        }
        start = start * 10 + (digit - '0');
    }

    // running (with a week to spare) means nothing to do
    if (start <= now && now < start + RunningSeconds - 7 * 24 * 3600)
    {
        return 0;
    }

    text.replace(valueStart, valueEnd - valueStart, std::to_string(now - 24 * 3600));
    return 1;
}

// Recipe ids follow the existing pattern (0..4 by input rarity, +10 for
// StatTrak), which is also what the GC's craft handler expects.
const char *const GameSchemaPatch::GoldTradeUpRecipes = R"(		"5"
		{
			"name"		"#RT_MP_A"
			"n_A"		"#RI_R6p"
			"desc_inputs"		"#RDI_AB"
			"desc_outputs"		"#RDO_AB"
			"di_A"		"5"
			"di_B"		"#RI_R6p"
			"do_A"		"1"
			"do_B"		"#Exceedingly_Rare_Item"
			"all_same_class"		"0"
			"always_known"		"1"
			"premium_only"		"0"
			"disabled"		"0"
			"input_items"
			{
				"5"
				{
					"conditions"
					{
						"0" { "field" "*rarity" "operator" "string==" "value" "ancient" "required" "1" }
						"1" { "field" "*quality" "operator" "string==" "value" "unique" "required" "1" }
						"2" { "field" "craft_class" "operator" "string==" "value" "weapon" "required" "1" }
					}
				}
			}
			"output_items"
			{
				"item1"
				{
					"conditions"
					{
						"0" { "field" "*match_set_rarity" "operator" "string==" "value" "unusual" "required" "1" }
					}
				}
			}
			"category"		"crafting"
			"filter"		"-3"
		}
		"15"
		{
			"name"		"#RT_MP_A"
			"n_A"		"#RI_R6p"
			"desc_inputs"		"#RDI_AB"
			"desc_outputs"		"#RDO_AB"
			"di_A"		"5"
			"di_B"		"#RI_R6p"
			"do_A"		"1"
			"do_B"		"#Exceedingly_Rare_Item"
			"all_same_class"		"0"
			"always_known"		"1"
			"premium_only"		"0"
			"disabled"		"0"
			"input_items"
			{
				"5"
				{
					"conditions"
					{
						"0" { "field" "*rarity" "operator" "string==" "value" "ancient" "required" "1" }
						"1" { "field" "*quality" "operator" "string==" "value" "strange" "required" "1" }
						"2" { "field" "*kill_eater_score_type" "operator" "string==" "value" "0" "required" "1" }
						"3" { "field" "craft_class" "operator" "string==" "value" "weapon" "required" "1" }
					}
				}
			}
			"output_items"
			{
				"item1"
				{
					"conditions"
					{
						"0" { "field" "*match_set_rarity" "operator" "string==" "value" "unusual" "required" "1" }
						"1" { "field" "*stattrak_recipe" "operator" "string==" "value" "yes" "required" "1" }
					}
				}
			}
			"category"		"crafting"
			"filter"		"-3"
			"requires_tool"		"0"
		}
		"6"
		{
			"name"		"#RT_MP_A"
			"n_A"		"#Exceedingly_Rare_Item"
			"desc_inputs"		"#RDI_AB"
			"desc_outputs"		"#RDO_AB"
			"di_A"		"5"
			"di_B"		"#Exceedingly_Rare_Item"
			"do_A"		"1"
			"do_B"		"#Exceedingly_Rare_Item"
			"all_same_class"		"0"
			"always_known"		"1"
			"premium_only"		"0"
			"disabled"		"0"
			"input_items"
			{
				"5"
				{
					"conditions"
					{
						"0" { "field" "*quality" "operator" "string==" "value" "unusual" "required" "1" }
					}
				}
			}
			"output_items"
			{
				"item1"
				{
					"conditions"
					{
						"0" { "field" "*match_set_rarity" "operator" "string==" "value" "unusual" "required" "1" }
					}
				}
			}
			"category"		"crafting"
			"filter"		"-3"
		}
)";

bool GameSchemaPatch::InsertRecipes(std::string &text, const std::string &recipesText)
{
    std::vector<std::string> stack;
    std::string lastString;
    bool haveLastString = false;

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
            continue;
        }

        if (c == '}')
        {
            if (stack.size() == 2 && stack[0] == "items_game" && stack[1] == "recipes")
            {
                text.insert(i, recipesText);
                return true;
            }

            if (!stack.empty())
            {
                stack.pop_back();
            }
            haveLastString = false;
        }
    }

    return false;
}

// finds the body of items_game/recipes: [begin, end) between its braces
static bool FindRecipesBlock(const std::string &text, size_t &begin, size_t &end)
{
    std::vector<std::string> stack;
    std::string lastString;
    bool haveLastString = false;

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
            if (stack.size() == 2 && stack[0] == "items_game" && stack[1] == "recipes")
            {
                begin = i + 1;
            }
            continue;
        }

        if (c == '}')
        {
            if (stack.size() == 2 && stack[0] == "items_game" && stack[1] == "recipes")
            {
                end = i;
                return true;
            }

            if (!stack.empty())
            {
                stack.pop_back();
            }
            haveLastString = false;
        }
    }

    return false;
}

size_t GameSchemaPatch::RelaxTradeUpQuality(std::string &text)
{
    size_t begin = 0, end = 0;
    if (!FindRecipesBlock(text, begin, end))
    {
        return 0;
    }

    // "field" "*quality" "operator" "string==" "value" "unique"
    //   -> "field" "*quality" "operator" "string!=" "value" "unusual"
    static const std::regex condition{
        R"re(("field"\s+"\*quality"\s+"operator"\s+)"string=="(\s+"value"\s+)"unique")re" };

    std::string body = text.substr(begin, end - begin);
    size_t count = 0;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), condition); it != std::sregex_iterator(); ++it)
    {
        count++;
    }

    if (count)
    {
        body = std::regex_replace(body, condition, R"($1"string!="$2"unusual")");
        text.replace(begin, end - begin, body);
    }

    return count;
}

static std::string AbsolutePath(const char *path)
{
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? std::string{ path } : absolute.string();
}

bool GameSchemaPatch::Apply(const LootListPreviews &previews, const std::string &recipesText, bool mixQualities,
    const StarShopPatch *starShop, const std::vector<AddedCasePatch> *addedCases)
{
    const std::string schemaPath = AbsolutePath(SchemaPath);
    Platform::Print("items_game.txt patch: file %s, backup %s\n", schemaPath.c_str(),
        FileExists(BackupPath) ? "exists" : "not created yet");

    std::string original;
    if (!ReadWholeFile(OriginalSchemaPath(), original))
    {
        Platform::Print("items_game.txt patch: could not read %s (errno %d), not patching it\n",
            OriginalSchemaPath(), errno);
        return false;
    }

    std::string patched = original;
    size_t replaced = PatchLootLists(patched, previews);
    if (replaced != previews.size())
    {
        Platform::Print("custom_cases: found %zu of %zu case contents lists to update\n", replaced, previews.size());
    }

    if (!recipesText.empty() && !InsertRecipes(patched, recipesText))
    {
        Platform::Print("items_game.txt patch: no recipes block found, gold trade-up recipes not added\n");
    }

    size_t relaxed = 0;
    if (mixQualities)
    {
        relaxed = RelaxTradeUpQuality(patched);
        if (!relaxed)
        {
            Platform::Print("items_game.txt patch: no normal-quality trade-up recipes found to allow StatTrak mixing\n");
        }
    }

    int starGoodsReplaced = -1;
    if (starShop)
    {
        starGoodsReplaced = ReplaceRedeemables(patched, starShop->season, starShop->blocksText);
        if (starGoodsReplaced < 0)
        {
            Platform::Print("items_game.txt patch: no seasonaloperations block for season %u, star shop goods not added\n",
                starShop->season);
        }
        else
        {
            Platform::Print("items_game.txt patch: star shop replaces %d operation page good(s) of season %u\n",
                starGoodsReplaced, starShop->season);
        }
    }

    if (addedCases && !addedCases->empty())
    {
        const size_t count = AddCases(patched, *addedCases);
        Platform::Print("items_game.txt patch: added %zu of %zu custom case(s) as new items\n", count, addedCases->size());
    }

    if (starShop && starShop->activateOperation)
    {
        const int moved = ActivateOperation(patched, static_cast<int64_t>(time(nullptr)));
        if (moved < 0)
        {
            Platform::Print("items_game.txt patch: no quest_schedule start found, the operation page may stay hidden\n");
        }
        else
        {
            Platform::Print(moved ? "items_game.txt patch: operation schedule moved to now so its page shows\n"
                                  : "items_game.txt patch: operation schedule already running\n");
        }
    }

    std::string current;
    if (ReadWholeFile(SchemaPath, current) && current == patched)
    {
        Platform::Print("items_game.txt patch: already up to date (%zu case preview(s)%s%s)\n",
            replaced, recipesText.empty() ? "" : ", gold trade-up recipes", relaxed ? ", StatTrak mixing" : "");
        return false;
    }

    if (!FileExists(BackupPath) && !WriteWholeFile(BackupPath, original))
    {
        Platform::Print("items_game.txt patch: could not create the backup %s (errno %d), not patching\n",
            BackupPath, errno);
        return false;
    }

    if (!WriteWholeFile(SchemaPath, patched))
    {
        Platform::Print("items_game.txt patch: could not write %s (errno %d)\n", schemaPath.c_str(), errno);
        return false;
    }

    Platform::Print("items_game.txt patch: updated %s (%zu case preview(s)%s%s), restart the game to see the changes\n",
        schemaPath.c_str(), replaced, recipesText.empty() ? "" : ", gold trade-up recipes",
        relaxed ? ", StatTrak mixing" : "");
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
        Platform::Print("could not restore %s from %s\n", SchemaPath, BackupPath);
        return false;
    }

    remove(BackupPath);
    Platform::Print("restored the original %s, restart the game to see it\n", SchemaPath);
    return true;
}
