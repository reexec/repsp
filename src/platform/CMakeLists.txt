if(WIN32)
    target_sources(repsp_platform PRIVATE
        win32/dynlib_win32.cpp
        win32/virtual_memory_win32.cpp
        win32/guarded_run_win32.cpp
        win32/host_thread_win32.cpp
        win32/path_win32.cpp
    )
else()
    target_sources(repsp_platform PRIVATE
        linux/dynlib_linux.cpp
        linux/virtual_memory_linux.cpp
        linux/guarded_run_linux.cpp
        linux/host_thread_linux.cpp
        linux/path_linux.cpp
    )
    target_link_libraries(repsp_platform PRIVATE ${CMAKE_DL_LIBS})
endif()

target_sources(repsp_platform PRIVATE
    sdl/sdl_platform.cpp
)
