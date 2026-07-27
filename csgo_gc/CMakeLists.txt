project(csgo_gc)

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

file(GLOB PROTOBUFS ../protobufs/*.cc)
target_include_directories(csgo_gc PRIVATE ../protobufs)
target_sources(csgo_gc PRIVATE ${PROTOBUFS})
source_group("Protobufs" FILES ${PROTOBUFS})

target_link_libraries(csgo_gc PRIVATE
    cryptopp
    funchook-static
    libprotobuf-lite
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
        gc_message_tests.cpp
        gc_message.cpp)

    target_precompile_headers(gc_message_tests PRIVATE stdafx.h)
    target_include_directories(gc_message_tests PRIVATE ../protobufs)
    target_sources(gc_message_tests PRIVATE ${PROTOBUFS})
    target_link_libraries(gc_message_tests PRIVATE libprotobuf-lite)

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
    target_include_directories(item_schema_tests PRIVATE ../protobufs)
    target_sources(item_schema_tests PRIVATE ${PROTOBUFS})
    target_link_libraries(item_schema_tests PRIVATE libprotobuf-lite)

    if (MSVC)
        target_compile_options(item_schema_tests PRIVATE /W4 /wd4244 /wd4819)
        target_compile_definitions(item_schema_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(item_schema_tests PRIVATE -Wall -Wextra)
    endif()

    add_test(NAME item_schema_tests COMMAND item_schema_tests)
endif()
