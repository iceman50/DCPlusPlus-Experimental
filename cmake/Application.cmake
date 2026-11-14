# Copyright (C) 2026 iceman50
set(generated "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated}/dcpp" "${generated}/help")
# Copy just this translation unit so quoted includes select CMake's own metadata.
configure_file("${DCPP_ROOT}/dcpp/core/version.cpp" "${generated}/dcpp/version.cpp" COPYONLY)
file(CONFIGURE OUTPUT "${generated}/dcpp/version-build.inc"
    CONTENT "#define DCPP_BUILD_NUMBER @DCPP_BUILD_NUMBER@\n" @ONLY)
add_custom_target(cmake_revision
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" revision
        "${DCPP_ROOT}" "${generated}/dcpp/version-rev-id.inc"
    BYPRODUCTS "${generated}/dcpp/version-rev-id.inc"
    VERBATIM)
file(GLOB help_html CONFIGURE_DEPENDS "${DCPP_ROOT}/help/*.html")
list(FILTER help_html EXCLUDE REGEX "/(changelog|compile|fdl)\\.html$")
configure_file("${DCPP_ROOT}/help/resource.h" "${generated}/help/resource.h" COPYONLY)
add_custom_command(OUTPUT "${generated}/help/cshelp.h"
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/generate.py" help
        "${DCPP_ROOT}" "${generated}/help/cshelp.h"
    DEPENDS ${help_html} "${DCPP_ROOT}/help/gen_cshelp.py" "${DCPP_ROOT}/build_util.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/generate.py"
    VERBATIM)
add_custom_target(cmake_help_ids DEPENDS "${generated}/help/cshelp.h")

file(GLOB_RECURSE core_sources CONFIGURE_DEPENDS "${DCPP_ROOT}/dcpp/*.cpp")
file(GLOB_RECURSE core_headers CONFIGURE_DEPENDS "${DCPP_ROOT}/dcpp/*.h")
list(REMOVE_ITEM core_sources "${DCPP_ROOT}/dcpp/core/version.cpp")
add_library(dcpp STATIC ${core_sources} ${core_headers} "${generated}/dcpp/version.cpp")
source_group(TREE "${DCPP_ROOT}/dcpp" PREFIX Core FILES ${core_sources} ${core_headers})
add_dependencies(dcpp cmake_revision)
target_compile_definitions(dcpp PRIVATE BUILDING_DCPP=1)
target_include_directories(dcpp PRIVATE "${DCPP_ROOT}/dcpp")
target_link_libraries(dcpp PUBLIC dcpp_options zlib zstd sqlite bzip2 maxminddb miniupnpc natpmp md4c aintl
    openssl_ssl gdi32 iphlpapi normaliz ole32 ws2_32)

file(GLOB_RECURSE dwt_sources CONFIGURE_DEPENDS "${DCPP_ROOT}/dwt/src/*.cpp")
file(GLOB_RECURSE dwt_headers CONFIGURE_DEPENDS "${DCPP_ROOT}/dwt/include/*.h")
add_library(dwt STATIC ${dwt_sources} ${dwt_headers})
source_group(TREE "${DCPP_ROOT}/dwt" PREFIX DWT FILES ${dwt_sources} ${dwt_headers})
target_link_libraries(dwt PUBLIC dcpp_options comctl32 comdlg32 gdi32 ole32 oleaut32 shlwapi uuid uxtheme winmm wtsapi32 dwmapi)
target_include_directories(dwt PUBLIC "${DCPP_ROOT}/dwt/include")

add_library(gui_options INTERFACE)
target_link_libraries(gui_options INTERFACE dcpp dwt windowscodecs psapi crypt32)
target_include_directories(gui_options INTERFACE "${DCPP_ROOT}/win32")
# Force the generated help-resource header first: its guard suppresses the source
# copy, including any stale SCons cshelp.h. This also works in a clean checkout.
if(MINGW)
    target_compile_options(gui_options INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include \"${generated}/help/resource.h\">")
    target_link_libraries(gui_options INTERFACE dwarf imagehlp)
else()
    target_compile_options(gui_options INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/FI${generated}/help/resource.h>")
    target_link_libraries(gui_options INTERFACE dbghelp)
    # The resource script already embeds the application's complete manifest.
    target_link_options(gui_options INTERFACE /MANIFEST:NO /MANIFESTUAC:NO)
endif()
include(CheckIncludeFiles)
include(CheckLibraryExists)
check_include_files("windows.h;htmlhelp.h" have_htmlhelp_header LANGUAGE CXX)
check_library_exists(htmlhelp HtmlHelpW "" have_htmlhelp_library)
if(have_htmlhelp_header AND have_htmlhelp_library)
    target_compile_definitions(gui_options INTERFACE HAVE_HTMLHELP_H)
    target_link_libraries(gui_options INTERFACE htmlhelp)
endif()

file(GLOB_RECURSE gui_sources CONFIGURE_DEPENDS "${DCPP_ROOT}/win32/*.cpp")
file(GLOB_RECURSE gui_headers CONFIGURE_DEPENDS "${DCPP_ROOT}/win32/*.h")
list(REMOVE_ITEM gui_sources "${DCPP_ROOT}/win32/main.cpp")
add_library(win32_objects OBJECT ${gui_sources} ${gui_headers})
source_group(TREE "${DCPP_ROOT}/win32" PREFIX GUI FILES ${gui_sources} ${gui_headers})
target_link_libraries(win32_objects PUBLIC gui_options)
add_dependencies(win32_objects cmake_help_ids)
if(DCPP_PCH)
    target_compile_definitions(dcpp PRIVATE HAS_PCH)
    target_precompile_headers(dcpp PRIVATE "${DCPP_ROOT}/dcpp/stdinc.h")
    target_compile_definitions(win32_objects PRIVATE HAS_PCH)
    target_precompile_headers(win32_objects PRIVATE "${DCPP_ROOT}/win32/stdafx.h")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${DCPP_ROOT}/win32/DCPlusPlus.rc")
file(READ "${DCPP_ROOT}/win32/DCPlusPlus.rc" resource_contents)
file(CONFIGURE OUTPUT "${generated}/DCPlusPlus.rc"
    CONTENT "#include \"@generated@/help/resource.h\"\n@resource_contents@" @ONLY)
file(GLOB_RECURSE resource_assets CONFIGURE_DEPENDS "${DCPP_ROOT}/res/*.ico" "${DCPP_ROOT}/res/*.bmp" "${DCPP_ROOT}/res/*.png")
set_source_files_properties("${generated}/DCPlusPlus.rc" PROPERTIES
    COMPILE_DEFINITIONS "${DCPP_ARCH}"
    OBJECT_DEPENDS "${generated}/help/cshelp.h;${DCPP_ROOT}/res/DCPlusPlus.manifest.${DCPP_ARCH};${resource_assets}")
add_executable(DCPlusPlus WIN32 "${DCPP_ROOT}/win32/main.cpp" "${generated}/DCPlusPlus.rc")
target_link_libraries(DCPlusPlus PRIVATE win32_objects)
add_dependencies(DCPlusPlus cmake_help_ids)
if(MINGW)
    # Keep the linker's output intact; either missing published output can be
    # regenerated without relinking or extracting symbols from a stripped EXE.
    set_target_properties(DCPlusPlus PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/linked/$<CONFIG>")
    set(published "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
    add_custom_command(OUTPUT "${published}/DCPlusPlus.exe" "${published}/DCPlusPlus.pdb"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${published}"
        COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:DCPlusPlus>" "${published}/DCPlusPlus.pdb"
        COMMAND "${CMAKE_OBJCOPY}" --strip-all "$<TARGET_FILE:DCPlusPlus>" "${published}/DCPlusPlus.exe"
        COMMAND "${CMAKE_OBJCOPY}" "--add-gnu-debuglink=${published}/DCPlusPlus.pdb" "${published}/DCPlusPlus.exe"
        DEPENDS DCPlusPlus VERBATIM)
    add_custom_target(application ALL DEPENDS "${published}/DCPlusPlus.exe" "${published}/DCPlusPlus.pdb")
else()
    add_custom_target(application ALL DEPENDS DCPlusPlus)
endif()
