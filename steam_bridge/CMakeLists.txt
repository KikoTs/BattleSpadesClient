cmake_minimum_required(VERSION 3.25)

project(BattleSpadesSteamBridge LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "BattleSpadesSteamBridge must be built with 32-bit MSVC")
endif()

add_executable(BattleSpadesSteamBridge32 steam_bridge_main.cpp)
target_compile_features(BattleSpadesSteamBridge32 PRIVATE cxx_std_20)
target_compile_definitions(
    BattleSpadesSteamBridge32
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
)
target_compile_options(BattleSpadesSteamBridge32 PRIVATE /W4 /WX /permissive- /EHsc)
target_link_libraries(BattleSpadesSteamBridge32 PRIVATE shell32)
set_property(
    TARGET BattleSpadesSteamBridge32
    PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

