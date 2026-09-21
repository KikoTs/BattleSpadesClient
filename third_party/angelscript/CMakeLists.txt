file(GLOB ANGELSCRIPT_SOURCES CONFIGURE_DEPENDS source/*.cpp)
add_library(aos_angelscript STATIC ${ANGELSCRIPT_SOURCES}
    addons/scriptarray.cpp addons/scriptstdstring.cpp addons/scriptmath.cpp)
# SYSTEM: vendored headers must not fail the client's -Werror translation units.
target_include_directories(aos_angelscript SYSTEM PUBLIC include addons)
target_compile_definitions(aos_angelscript PUBLIC AS_MAX_PORTABILITY)
set_target_properties(aos_angelscript PROPERTIES CXX_STANDARD 17)
if(MSVC)
    target_compile_options(aos_angelscript PRIVATE /w)
else()
    target_compile_options(aos_angelscript PRIVATE -w)
endif()
