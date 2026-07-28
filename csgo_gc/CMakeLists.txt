project(csgo_gc)

find_package(MbedTLS CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# funchook and its deps get pulled in via FetchContent
# FIXME: consider alternatives!!! funchook is no longer
# maintained, and our hooks won't work reliably on x86_64
# macOS when using it
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

option(FUNCHOOK_BUILD_SHARED "build shared library" OFF)
option(FUNCHOOK_BUILD_TESTS "Build tests" OFF)
option(FUNCHOOK_INSTALL "Install Funchook" OFF)

FetchContent_Declare(funchook
	GIT_REPOSITORY https://github.com/kubo/funchook
	GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(funchook)

add_library(csgo_gc SHARED
    appid.cpp
    case_opening.cpp
    config.cpp
    gc_client.cpp
    gc_message.cpp
    gc_server.cpp
    gc_shared.cpp
    graffiti.cpp
    inventory.cpp
    item_schema.cpp
    item_utils.cpp
    keyvalue.cpp
    main.cpp
    networking_client.cpp
    networking_server.cpp
    rcon_server.cpp
    souvenir.cpp
    steam_hook.cpp)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_sources(csgo_gc PRIVATE platform_windows.cpp)
else()
    target_sources(csgo_gc PRIVATE platform_unix.cpp)
endif()

target_precompile_headers(csgo_gc PRIVATE stdafx.h)

set_target_properties(csgo_gc PROPERTIES PREFIX "")

if (MSVC)
    target_compile_options(csgo_gc PRIVATE /W4 /wd4244 /wd4819)
    target_compile_definitions(csgo_gc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(csgo_gc PRIVATE -Wall -Wextra)
endif()

set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protobufs)
file(GLOB PROTOS ${PROTO_DIR}/*.proto)
set(PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${PROTOC_OUT_DIR})

add_library(csgo_gc_protobufs STATIC)
set_target_properties(csgo_gc_protobufs PROPERTIES POSITION_INDEPENDENT_CODE ON)

protobuf_generate(
    TARGET csgo_gc_protobufs
    PROTOS ${PROTOS}
    IMPORT_DIRS ${PROTO_DIR}
    PROTOC_OUT_DIR ${PROTOC_OUT_DIR})

source_group("Protobufs" REGULAR_EXPRESSION "\\.pb\\.(cc|h)$")

target_include_directories(csgo_gc_protobufs PUBLIC ${PROTOC_OUT_DIR})
target_link_libraries(csgo_gc_protobufs PUBLIC protobuf::libprotobuf-lite)

target_link_libraries(csgo_gc PRIVATE
    MbedTLS::mbedcrypto
    csgo_gc_protobufs
    funchook-static
    steam_api
    Threads::Threads)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_link_libraries(csgo_gc PRIVATE ws2_32)
endif()

# apple message box
if (APPLE)
    target_link_libraries(csgo_gc PRIVATE "-framework Foundation")
endif()

if (OUTDIR)
    add_custom_command(TARGET csgo_gc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:csgo_gc>
        ${OUTDIR}/csgo_gc/${GC_LIB_DIR}/$<TARGET_FILE_NAME:csgo_gc>)
endif()

if (BUILD_TESTING)
    add_executable(gc_message_tests
        case_opening.cpp
        config.cpp
        gc_client.cpp
        gc_shared.cpp
        graffiti.cpp
        gc_message_tests.cpp
        gc_message.cpp
        inventory.cpp
        item_schema.cpp
        item_utils.cpp
        keyvalue.cpp
        souvenir.cpp)

    target_precompile_headers(gc_message_tests PRIVATE stdafx.h)
    target_link_libraries(gc_message_tests PRIVATE
        MbedTLS::mbedcrypto
        csgo_gc_protobufs
        steam_api
        Threads::Threads)

    if (MSVC)
        target_compile_options(gc_message_tests PRIVATE /W4 /wd4244 /wd4819)
        target_compile_definitions(gc_message_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(gc_message_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME gc_message_tests COMMAND gc_message_tests)

    add_executable(item_schema_tests
        item_schema_tests.cpp
        item_schema.cpp
        keyvalue.cpp)

    target_precompile_headers(item_schema_tests PRIVATE stdafx.h)
    target_link_libraries(item_schema_tests PRIVATE csgo_gc_protobufs)

    if (MSVC)
        target_compile_options(item_schema_tests PRIVATE /W4 /wd4244 /wd4819)
        target_compile_definitions(item_schema_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(item_schema_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME item_schema_tests COMMAND item_schema_tests)
endif()
