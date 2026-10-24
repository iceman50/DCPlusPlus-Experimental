# Set DCPP_MINGW_ROOT in the environment to select a toolchain outside PATH.
set(CMAKE_SYSTEM_NAME Windows)
foreach(pair IN ITEMS "C;gcc" "CXX;g++" "RC;windres")
    list(GET pair 0 language)
    list(GET pair 1 program)
    find_program(DCPP_${language}_COMPILER NAMES ${program}
        HINTS "$ENV{DCPP_MINGW_ROOT}/bin" REQUIRED)
    set(CMAKE_${language}_COMPILER "${DCPP_${language}_COMPILER}")
endforeach()
