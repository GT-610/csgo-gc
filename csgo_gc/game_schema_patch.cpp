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

bool GameSchemaPatch::Apply(const LootListPreviews &previews, const std::string &recipesText)
{
    std::string original;
    if (!ReadWholeFile(OriginalSchemaPath(), original))
    {
        Platform::Print("could not read %s, not patching it\n", SchemaPath);
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
        Platform::Print("items_game.txt has no recipes block, gold trade-up recipes not added\n");
    }

    std::string current;
    if (ReadWholeFile(SchemaPath, current) && current == patched)
    {
        return false; // already up to date
    }

    if (!FileExists(BackupPath) && !WriteWholeFile(BackupPath, original))
    {
        Platform::Print("could not back up %s, not patching it\n", SchemaPath);
        return false;
    }

    if (!WriteWholeFile(SchemaPath, patched))
    {
        Platform::Print("could not write %s\n", SchemaPath);
        return false;
    }

    Platform::Print("Updated %s (%zu case preview(s)%s), restart the game to see the changes\n",
        SchemaPath, replaced, recipesText.empty() ? "" : ", gold trade-up recipes");
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
