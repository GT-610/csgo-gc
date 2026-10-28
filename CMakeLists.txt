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
        # when an object references __tls_get_addr. That tag is a safety
        # assertion, not just a version number: it says the loader's GNU TLS
        # run-time preserves XMM registers across ___tls_get_addr, which older
        # glibc does not (BZ 32996). Dropping it lets a binary silently corrupt
        # floating-point state on those systems instead of refusing to start.
        #
        # The tag also raises the minimum glibc to 2.42, which is what broke
        # Ubuntu 24.04 in issue #143. The supported way to avoid it is to build
        # against the old glibc (the release job runs in an ubuntu:22.04
        # container), not to suppress the assertion here. So keep the tag by
        # default and require an explicit opt-in to drop it.
        option(CSGO_GC_SUPPRESS_GNU_TLS_TAG
            "Link i386 builds without the GLIBC_ABI_GNU_TLS safety tag. Only do \
this if the resulting binary is known to be safe on glibc without the GNU TLS \
run-time fix; it makes floating-point corruption possible instead of a clean \
load failure." OFF)

        if (IS_32BIT AND CSGO_GC_SUPPRESS_GNU_TLS_TAG)
            include(CheckLinkerFlag)
            check_linker_flag(CXX "LINKER:--no-gnu-tls-tag" HAVE_NO_GNU_TLS_TAG)
            if (HAVE_NO_GNU_TLS_TAG)
                add_link_options("LINKER:--no-gnu-tls-tag")
            else()
                message(WARNING
                    "CSGO_GC_SUPPRESS_GNU_TLS_TAG is ON but the linker does not "
                    "support --no-gnu-tls-tag; the tag will be kept.")
            endif()
        endif()
    endif()
endif()

add_subdirectory(csgo_gc)
add_subdirectory(launcher)
add_subdirectory(steamworks)
