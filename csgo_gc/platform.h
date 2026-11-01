#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace Platform
{

// called right after we load into the process
void Initialize();

// print a debugging message to the in game console or something
void Print(const char *format, ...);

// fatal error, show a message box if possible and exit the program
[[noreturn]] void Error(const char *format, ...);

// get steamclient's path without incrementing its refcount
// buffer is only accessed by the platform code, on
// windows we stick utf16 to it and utf8 on other platforms
bool SteamClientPath(void *buffer, size_t bufferSize);

// load a dynamic library from the provided path and increment its refcount
// note that the path is UTF-16 on windows and UTF-8 on the other ones
void *LoadDynamicLibrary(const void *pathBuffer);

// GetProcAddress/dlsym
void *GetSymbol(void *handle, const char *symbol);

// get a module's factory function from an already loaded game module
// the module name is given in a platform-agnostic format
// (e.g. "engine" maps to engine.dll / engine_client.so / engine.dylib)
void *ModuleFactory(std::string_view moduleName);

// set an envar to the specified value even if it's already set
void SetEnvVar(const char *name, const char *value);

// safely write to memory that may be read-only, handles protection changes internally
// set needsExecute to true when modifying code sections
bool WriteToProtectedMemory(void *address, const void *data, size_t size, bool needsExecute);

// Find the executable section of an already loaded game module.
// The module name is given in a platform-agnostic format ("panorama" maps to
// panorama.dll / panorama_client.so / panorama.dylib). Returns false when the
// module is not loaded, which is the normal case for modules the game loads
// lazily after startup.
bool GetModuleCodeSection(std::string_view moduleName, uint8_t **start, uint8_t **end);

// Number of bits in an already loaded module (32 or 64), or 0 when the module is
// not loaded or its format is not recognised. Callers that decode instructions need
// this because the decode width must match the module, and guessing it would
// silently mis-decode a module of the other kind.
int GetModuleBitness(std::string_view moduleName);

// Readable byte ranges of an already loaded module, as [start, end) pairs, covering
// the data sections as well as the code.
//
// The image as a whole is not one contiguous readable span: section alignment
// leaves padding between sections, and that padding belongs to no section and is
// not guaranteed to be mapped. Callers that scan for content must walk these
// intervals rather than a single range, or they will fault on a module whose
// segments are separated by unmapped pages.
// Returns false when the module is not loaded.
bool GetModuleReadableRanges(std::string_view moduleName, std::vector<std::pair<uint8_t *, uint8_t *>> &ranges);

// update the graffiti public key in the specified module to get sprays working
// the module name is given in a platform-agnostic format
// (e.g. on linux pass server as moduleName, and it'll operate on server_client.so)
bool UpdateGraffitiKey(std::string_view moduleName, const void *original, const void *replacement, size_t size);

// returns true if serverbrowser was loaded and we updated it
bool UpdateServerBrowserAppId(uint32_t appId);

} // namespace Platform
