if(NOT AOS_TARGET_SYSTEM STREQUAL "Haiku")
    return()
endif()
# RmlUi 6.3 assumes every CMake >= 3.21 supports runtime dependency scanning.
# CMake only implements that feature on Windows, Linux and macOS. We link
# RmlUi statically and use Haiku packages for its system libraries.
set(_file "${AOS_RMLUI_SOURCE}/CMake/RuntimeUtilities.cmake")
file(READ "${_file}" _source)
set(_old [=[if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.21")]=])
set(_new [=[if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.21" AND NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")]=])
string(FIND "${_source}" "${_new}" _already_patched)
if(_already_patched EQUAL -1)
    string(FIND "${_source}" "${_old}" _offset)
    if(_offset EQUAL -1)
        message(FATAL_ERROR "RmlUi's runtime dependency setup changed; review the Haiku patch")
    endif()
    string(REPLACE "${_old}" "${_new}" _source "${_source}")
    file(WRITE "${_file}" "${_source}")
endif()
