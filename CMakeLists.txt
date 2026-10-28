cmake_minimum_required(VERSION 3.20)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "macOS deployment target")

project(csgo_gc)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(IS_32BIT ON)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if (IS_32BIT)
        set(GC_LIB_DIR ".")
        set(GC_EXE_SUFFIX "_osx")
    else()
        set(GC_LIB_DIR "osx64")
        set(GC_EXE_SUFFIX "_osx64")
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (IS_32BIT)
        set(GC_LIB_DIR ".")
        set(GC_EXE_SUFFIX "_linux")
    else()
        set(GC_LIB_DIR "linux64")
        set(GC_LIB_SUFFIX "_client")
        set(GC_EXE_SUFFIX "_linux64")
    endif()
elseif (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if (IS_32BIT)
        set(GC_LIB_DIR ".")
    else()
        set(GC_LIB_DIR "x64")
        set(GC_EXE_SUFFIX "_win64")
    endif()
else()
    message(FATAL_ERROR "Unsupported system $[CMAKE_SYSTEM_NAME}")
endif()

if (MSVC)
    # statically linked crt
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-fpic -fvisibility=hidden)

    if (NOT APPLE)
        add_link_options(-Wl,--no-undefined -z noexecstack)

        # binutils 2.45+ records a GLIBC_ABI_GNU_TLS version dependency on i386
        # when an object references __tls_get_addr and libc.so defines that
        # version. That silently raises the minimum glibc of the shipped .so to
        # 2.42+, so a build made on a newer distro refuses to load on 24.04 with
        # "version `GLIBC_ABI_GNU_TLS' not found". Suppress it when the linker
        # knows how; older linkers that lack the option need no suppression
        # because they never emit the tag. The option is i386-only, hence the
        # IS_32BIT guard, and we probe instead of assuming so that a future
        # runner image change cannot reintroduce the dependency. See issue #143.
        if (IS_32BIT)
            include(CheckLinkerFlag)
            check_linker_flag(CXX "LINKER:--no-gnu-tls-tag" HAVE_NO_GNU_TLS_TAG)
            if (HAVE_NO_GNU_TLS_TAG)
                add_link_options("LINKER:--no-gnu-tls-tag")
            endif()
        endif()
    endif()
endif()

add_subdirectory(csgo_gc)
add_subdirectory(launcher)
add_subdirectory(steamworks)
