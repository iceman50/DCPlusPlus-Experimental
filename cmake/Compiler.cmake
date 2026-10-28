# Copyright (C) 2026 iceman50
# Match the production SCons compiler policy without importing its Python state.
set(DCPP_CPU_TARGET "default" CACHE STRING "Optional MinGW x64 instruction baseline")
set_property(CACHE DCPP_CPU_TARGET PROPERTY STRINGS default nehalem x86-64-v2 x86-64-v3)
if(NOT DCPP_CPU_TARGET MATCHES "^(default|nehalem|x86-64-v2|x86-64-v3)$")
    message(FATAL_ERROR "DCPP_CPU_TARGET must be default, nehalem, x86-64-v2 or x86-64-v3")
endif()
if(NOT DCPP_CPU_TARGET STREQUAL "default" AND (NOT MINGW OR NOT DCPP_ARCH STREQUAL "x64"))
    message(FATAL_ERROR "DCPP_CPU_TARGET requires MinGW x64; use default for other targets")
endif()
add_library(dcpp_options INTERFACE)
target_include_directories(dcpp_options INTERFACE "${DCPP_ROOT}")
target_compile_definitions(dcpp_options INTERFACE
    _WIN32_WINNT=0x601 WINVER=0x601 _WIN32_IE=0x0A00 NOMINMAX STRICT
    WIN32_LEAN_AND_MEAN UNICODE _UNICODE CASESENSITIVITYDEFAULT_YES ZLIB_WINAPI
    _REENTRANT "$<$<CONFIG:Debug>:_DEBUG>" "$<$<CONFIG:Release>:NDEBUG>")
if(MINGW)
    target_include_directories(dcpp_options INTERFACE
        "${DCPP_ROOT}/mingw/preload" "${DCPP_ROOT}/mingw/include")
    target_link_directories(dcpp_options INTERFACE "${DCPP_ROOT}/mingw/lib${DCPP_LIB_SUFFIX}")
    target_compile_definitions(dcpp_options INTERFACE NO_VIZ)
    target_compile_options(dcpp_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:-gdwarf-5;-Wall;-Wextra;-Wno-missing-field-initializers;-Wno-unknown-pragmas;-Wno-unused-parameter;-fexceptions;-pipe;-mthreads;-mtune=generic>"
        "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Debug>>:-Og>"
        "$<$<AND:$<COMPILE_LANGUAGE:C,CXX>,$<CONFIG:Release>>:-O3>")
    if(DCPP_ARCH STREQUAL "x64")
        set(cpu_target "${DCPP_CPU_TARGET}")
        if(cpu_target STREQUAL "default")
            set(cpu_target nehalem)
        endif()
        target_compile_options(dcpp_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:-march=${cpu_target}>")
    else()
        target_compile_options(dcpp_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:-march=nocona;-mincoming-stack-boundary=2;-mstackrealign>")
    endif()
    target_link_options(dcpp_options INTERFACE -static -mthreads -gdwarf-5
        -Wl,--enable-runtime-pseudo-reloc,--no-undefined,--nxcompat,--dynamicbase)
else()
    target_compile_options(dcpp_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/Zi;/FC;/wd4100;/wd4121;/wd4127;/wd4131;/wd4189;/wd4244;/wd4290;/wd4307;/wd4324;/wd4355;/wd4456;/wd4457;/wd4458;/wd4510;/wd4512;/wd4610;/wd4706;/wd4800;/wd4996;/wd4005>"
        "$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/GR;/Zm200;/Zc:lambda>")
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(DCPP_MSVC_COMPILE_JOBS 4 CACHE STRING "Compiler processes per Visual Studio project")
        if(NOT DCPP_MSVC_COMPILE_JOBS MATCHES "^[1-9][0-9]*$")
            message(FATAL_ERROR "DCPP_MSVC_COMPILE_JOBS must be positive")
        endif()
        target_compile_options(dcpp_options INTERFACE "$<$<COMPILE_LANGUAGE:C,CXX>:/MP${DCPP_MSVC_COMPILE_JOBS}>")
    endif()
    target_compile_definitions(dcpp_options INTERFACE "$<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0;_SECURE_SCL=0>")
    target_link_options(dcpp_options INTERFACE /DEBUG /FIXED:NO /INCREMENTAL:NO)
endif()
target_link_libraries(dcpp_options INTERFACE user32 shell32 advapi32)
