project(steam_api)

add_library(steam_api INTERFACE)

set(SDK_DIR ${CMAKE_CURRENT_LIST_DIR}/sdk)

target_include_directories(steam_api INTERFACE ${SDK_DIR}/public)

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
	set(STEAM_API_LIB "\"libsteam_api.dylib\"")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(STEAM_API_LIB "\"libsteam_api.so\"")
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
	if (IS_32BIT)
		# Windows library paths are passed to LoadLibraryExW.
		set(STEAM_API_LIB "L\"steam_api.dll\"")
	else()
		# Windows library paths are passed to LoadLibraryExW.
		set(STEAM_API_LIB "L\"steam_api64.dll\"")
	endif()
else()
	message(FATAL_ERROR "Unsupported system ${CMAKE_SYSTEM_NAME}")
endif()

# Load the Steam API dynamically so the proxy remains compatible with both old
# exported accessors and newer internal accessor implementations.
target_compile_definitions(steam_api INTERFACE STEAM_API_NODLL STEAM_API_LIB=${STEAM_API_LIB})
