#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

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

// Find the whole mapped image of an already loaded game module.
// This spans every section, not just the executable one, so callers that need to
// read string constants can scan the data sections too.
bool GetModuleImageRange(std::string_view moduleName, uint8_t **start, uint8_t **end);

// update the graffiti public key in the specified module to get sprays working
// the module name is given in a platform-agnostic format
// (e.g. on linux pass server as moduleName, and it'll operate on server_client.so)
bool UpdateGraffitiKey(std::string_view moduleName, const void *original, const void *replacement, size_t size);

// returns true if serverbrowser was loaded and we updated it
bool UpdateServerBrowserAppId(uint32_t appId);

} // namespace Platform
