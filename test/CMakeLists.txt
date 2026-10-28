# Copyright (C) 2026 iceman50
# The CMake test graph is opt-in and uses only CMake-built production objects.
set(valid_components dcpp dwt win32 zlib dwarf)
foreach(component IN LISTS DCPP_TEST_COMPONENTS)
    if(NOT component IN_LIST valid_components)
        message(FATAL_ERROR "Unknown test component: ${component}")
    endif()
    if(component STREQUAL "dwarf" AND NOT MINGW)
        message(FATAL_ERROR "The DWARF tests require MinGW")
    endif()
endforeach()
add_custom_target(component_tests)
set(test_workspace "${CMAKE_BINARY_DIR}/test/runtime/$<CONFIG>")
# Existing tests use paths relative to the repository root. Mirror only their
# input fixtures so their output files never overlap a SCons test run.
file(GLOB_RECURSE fixture_files CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/dcpp/data/*" "${DCPP_ROOT}/Themes/*.xml"
    "${DCPP_ROOT}/Emoticons/*.dcemo")
list(FILTER fixture_files EXCLUDE REGEX "/data/out/")
foreach(config IN ITEMS Debug Release)
    set(workspace "${CMAKE_BINARY_DIR}/test/runtime/${config}")
    file(MAKE_DIRECTORY "${workspace}/test/dcpp/data/out" "${workspace}/build")
    foreach(fixture IN LISTS fixture_files)
        file(RELATIVE_PATH relative "${DCPP_ROOT}" "${fixture}")
        configure_file("${fixture}" "${workspace}/${relative}" COPYONLY)
    endforeach()
endforeach()
function(register_component_test target component)
    # EXCLUDE_FROM_ALL alone does not exclude projects from VS Build Solution.
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/${component}/$<CONFIG>"
        EXCLUDE_FROM_DEFAULT_BUILD TRUE
        FOLDER "Tests/${component}")
    add_dependencies(component_tests ${target})
    add_test(NAME "${component}/${target}" COMMAND ${target} ${ARGN})
    set_tests_properties("${component}/${target}" PROPERTIES
        WORKING_DIRECTORY "${test_workspace}"
        LABELS "${component}" TIMEOUT 600 RUN_SERIAL TRUE)
endfunction()

if("dcpp" IN_LIST DCPP_TEST_COMPONENTS OR "win32" IN_LIST DCPP_TEST_COMPONENTS OR "dwarf" IN_LIST DCPP_TEST_COMPONENTS)
    add_library(dcpp_gtest STATIC EXCLUDE_FROM_ALL googletest/gtest-all.cpp support/gtest_main.cpp)
    target_link_libraries(dcpp_gtest PRIVATE dcpp_options)
    target_include_directories(dcpp_gtest PUBLIC googletest/include support PRIVATE googletest)
    set_target_properties(dcpp_gtest PROPERTIES FOLDER Tests EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endif()
foreach(component IN ITEMS dcpp win32 dwarf)
    if(NOT component IN_LIST DCPP_TEST_COMPONENTS)
        continue()
    endif()
    file(GLOB unit_sources CONFIGURE_DEPENDS "${component}/test*.cpp")
    add_executable(${component}_tests EXCLUDE_FROM_ALL ${unit_sources})
    target_link_libraries(${component}_tests PRIVATE dcpp_options dcpp_gtest)
    if(component STREQUAL "dwarf")
        target_link_libraries(${component}_tests PRIVATE dwarf imagehlp)
        set(companion "${CMAKE_BINARY_DIR}/test/dwarf/$<CONFIG>/dwarf_tests.pdb")
        add_custom_command(OUTPUT "${companion}"
            COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:dwarf_tests>" "${companion}"
            DEPENDS dwarf_tests VERBATIM)
        add_custom_target(dwarf_test_symbols DEPENDS "${companion}")
        add_dependencies(component_tests dwarf_test_symbols)
    else()
        target_link_libraries(${component}_tests PRIVATE dcpp dwt)
    endif()
    register_component_test(${component}_tests ${component} --gtest_fail_if_no_test_linked)
endforeach()

if("dwt" IN_LIST DCPP_TEST_COMPONENTS)
    file(GLOB dwt_tests CONFIGURE_DEPENDS "dwt/*.cpp")
    file(GLOB dwt_resources CONFIGURE_DEPENDS "dwt/*.rc")
    set_source_files_properties(${dwt_resources} PROPERTIES COMPILE_DEFINITIONS "${DCPP_ARCH}"
        OBJECT_DEPENDS "${DCPP_ROOT}/res/DCPlusPlus.manifest.${DCPP_ARCH}")
    set(manual_tests RichTextBoxTest SplitTest TableTest TableTreeTest TreeTest)
    foreach(source IN LISTS dwt_tests)
        get_filename_component(name "${source}" NAME_WE)
        if(name IN_LIST manual_tests)
            continue()
        endif()
        add_executable(${name} WIN32 EXCLUDE_FROM_ALL "${source}" ${dwt_resources})
        target_link_libraries(${name} PRIVATE dwt)
        if(MSVC)
            target_link_options(${name} PRIVATE /MANIFEST:NO)
        endif()
        register_component_test(${name} dwt)
    endforeach()
endif()

if("win32" IN_LIST DCPP_TEST_COMPONENTS)
    set_source_files_properties("${generated}/DCPlusPlus.rc" PROPERTIES
        COMPILE_DEFINITIONS "${DCPP_ARCH}"
        OBJECT_DEPENDS "${generated}/help/cshelp.h;${DCPP_ROOT}/res/DCPlusPlus.manifest.${DCPP_ARCH};${resource_assets}")
    file(GLOB integrations CONFIGURE_DEPENDS "win32/*Test.cpp")
    foreach(source IN LISTS integrations)
        get_filename_component(name "${source}" NAME_WE)
        add_executable(${name} WIN32 EXCLUDE_FROM_ALL "${source}" "${generated}/DCPlusPlus.rc")
        target_link_libraries(${name} PRIVATE win32_objects)
        add_dependencies(${name} cmake_help_ids)
        register_component_test(${name} win32)
    endforeach()
endif()

if("zlib" IN_LIST DCPP_TEST_COMPONENTS)
    foreach(name IN ITEMS example minigzip)
        add_executable(zlib_${name} EXCLUDE_FROM_ALL "zlib/${name}.c")
        target_link_libraries(zlib_${name} PRIVATE dcpp_options zlib)
        set_target_properties(zlib_${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test/zlib/$<CONFIG>"
            EXCLUDE_FROM_DEFAULT_BUILD TRUE FOLDER Tests/zlib)
        add_dependencies(component_tests zlib_${name})
        add_test(NAME "zlib/${name}" COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/zlib/run_cmake.py" ${name} "$<TARGET_FILE:zlib_${name}>")
        set_tests_properties("zlib/${name}" PROPERTIES LABELS zlib TIMEOUT 600)
    endforeach()
endif()
