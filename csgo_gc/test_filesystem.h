#pragma once

#include <cerrno>
#include <cstdio>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace TestFilesystem
{

inline bool MakeDirectory(const char *path)
{
#ifdef _WIN32
    const int result = _mkdir(path);
#else
    const int result = mkdir(path, 0755);
#endif
    return result == 0 || errno == EEXIST;
}

inline void RemoveFile(const char *path)
{
    std::remove(path);
}

inline void RemoveDirectory(const char *path)
{
#ifdef _WIN32
    _rmdir(path);
#else
    rmdir(path);
#endif
}

} // namespace TestFilesystem
