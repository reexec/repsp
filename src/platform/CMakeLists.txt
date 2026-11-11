if(WIN32)
    target_sources(repsp_platform PRIVATE
        win32/virtual_memory_win32.cpp
        win32/guarded_run_win32.cpp
        win32/host_thread_win32.cpp
        win32/path_win32.cpp
    )
else()
    target_sources(repsp_platform PRIVATE
        linux/virtual_memory_linux.cpp
        linux/guarded_run_linux.cpp
        linux/host_thread_linux.cpp
        linux/path_linux.cpp
    )
endif()

target_sources(repsp_platform PRIVATE
    sdl/sdl_platform.cpp
)
