# Host compiler flags and standards for repsp targets

function(repsp_apply_host_flags target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /utf-8
            /EHsc
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
    endif()
endfunction()
