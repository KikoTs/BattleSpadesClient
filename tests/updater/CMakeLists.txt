# Unit tests of the launcher/updater core (added from src/updater/CMakeLists.txt).
# Steam files are only ever fixture copies inside a temp folder.
set(_aos_updater_static_crt "MultiThreaded$<$<CONFIG:Debug>:Debug>")

foreach(_aos_updater_test IN ITEMS core steam apply manifest unicode launch download)
    set(_aos_target aos_updater_${_aos_updater_test}_tests)
    add_executable(${_aos_target} test_updater_${_aos_updater_test}.cpp)
    target_link_libraries(${_aos_target} PRIVATE BattleSpades::UpdaterCore)
    target_compile_definitions(
        ${_aos_target}
        PRIVATE AOS_UPDATER_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/steam"
                AOS_UPDATER_ZIP_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/zip"
    )
    set_property(TARGET ${_aos_target} PROPERTY MSVC_RUNTIME_LIBRARY "${_aos_updater_static_crt}")
    aos_enable_strict_warnings(${_aos_target})
    add_test(NAME ${_aos_target} COMMAND ${_aos_target})
    set_tests_properties(${_aos_target} PROPERTIES TIMEOUT 60)
endforeach()

if(WIN32)
    add_executable(aos_updater_http_probe updater_http_probe.cpp
        "${PROJECT_SOURCE_DIR}/src/updater/windows/win_util.cpp")
    target_link_libraries(aos_updater_http_probe PRIVATE BattleSpades::UpdaterCore winhttp shell32 ole32 advapi32)
    set_property(TARGET aos_updater_http_probe PROPERTY MSVC_RUNTIME_LIBRARY "${_aos_updater_static_crt}")
    aos_enable_strict_warnings(aos_updater_http_probe)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME aos_updater_http_tests COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/test_updater_http.py" "$<TARGET_FILE:aos_updater_http_probe>")
        set_tests_properties(aos_updater_http_tests PROPERTIES TIMEOUT 60)
    endif()
endif()

# Client-side Unicode paths (asset importer, local-server preparation). These
# libraries use the dynamic CRT, so this test cannot share the updater core.
find_package(unofficial-sodium CONFIG QUIET)
if(TARGET BattleSpades::AssetInstall AND TARGET BattleSpades::LocalHost AND TARGET unofficial-sodium::sodium)
    add_executable(aos_client_unicode_tests test_client_unicode.cpp)
    target_link_libraries(
        aos_client_unicode_tests
        PRIVATE BattleSpades::AssetInstall BattleSpades::LocalHost unofficial-sodium::sodium
    )
    aos_enable_strict_warnings(aos_client_unicode_tests)
    add_test(NAME aos_client_unicode_tests COMMAND aos_client_unicode_tests)
    set_tests_properties(aos_client_unicode_tests PROPERTIES TIMEOUT 60)
endif()
