function(aos_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(
            "${target}"
            PRIVATE
                /W4
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
                /EHsc
        )

        if(AOS_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            "${target}"
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                # Aggregates are extended with trailing members that positional
                # initializers deliberately leave value-initialized (e.g.
                # ServerConnectRequest::password); -Wextra would flag each use.
                -Wno-missing-field-initializers
        )

        if(AOS_WARNINGS_AS_ERRORS)
            target_compile_options("${target}" PRIVATE -Werror)
        endif()
    else()
        message(WARNING "Strict warnings are not configured for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
