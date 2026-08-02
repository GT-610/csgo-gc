#include "stdafx.h"
#include "keyvalue.h"
#include <cstdio>

namespace
{

constexpr const char *MissingPath = "keyvalue_test_missing.txt";
constexpr const char *EmptyPath = "keyvalue_test_empty.txt";
constexpr const char *MalformedPath = "keyvalue_test_malformed.txt";
constexpr const char *BomPath = "keyvalue_test_bom.txt";
constexpr const char *OutputPath = "keyvalue_test_output.txt";

void RemoveTestFiles()
{
    remove(MissingPath);
    remove(EmptyPath);
    remove(MalformedPath);
    remove(BomPath);
    remove(OutputPath);
    remove("keyvalue_test_output.txt.tmp");
}

bool WriteBytes(const char *path, std::string_view data)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        return false;
    }

    bool success = fwrite(data.data(), 1, data.size(), f) == data.size();
    success &= fclose(f) == 0;
    return success;
}

bool DetailedFileResultsAreReported()
{
    KeyValue key{ "test" };
    if (key.ParseFromFileDetailed(MissingPath) != KeyValueFileResult::NotFound)
    {
        return false;
    }

    if (!WriteBytes(EmptyPath, {})
        || key.ParseFromFileDetailed(EmptyPath) != KeyValueFileResult::Empty)
    {
        return false;
    }

    if (!WriteBytes(MalformedPath, "\"items\"\n{\n\"value\" \"42\"\n")
        || key.ParseFromFileDetailed(MalformedPath) != KeyValueFileResult::InvalidFormat)
    {
        return false;
    }

    return true;
}

bool Utf8BomIsAccepted()
{
    constexpr std::string_view Contents{ "\xef\xbb\xbf\"value\" \"42\"\n" };
    if (!WriteBytes(BomPath, Contents))
    {
        return false;
    }

    KeyValue key{ "test" };
    return key.ParseFromFileDetailed(BomPath) == KeyValueFileResult::Success
        && key.GetNumber<int>("value") == 42;
}

bool ExistingFileIsAtomicallyReplaced()
{
    if (!WriteBytes(OutputPath, "old contents"))
    {
        return false;
    }

    KeyValue output{ "test" };
    output.AddNumber("value", 1729);
    if (!output.WriteToFile(OutputPath))
    {
        return false;
    }

    KeyValue input{ "test" };
    return input.ParseFromFileDetailed(OutputPath) == KeyValueFileResult::Success
        && input.GetNumber<int>("value") == 1729;
}

} // namespace

int main()
{
    RemoveTestFiles();

    bool success = DetailedFileResultsAreReported()
        && Utf8BomIsAccepted()
        && ExistingFileIsAtomicallyReplaced();

    RemoveTestFiles();
    return success ? 0 : 1;
}
