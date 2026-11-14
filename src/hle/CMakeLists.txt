target_sources(repsp_hle_kernel PRIVATE
    kernel/hle_kernel.cpp
    kernel/sysmem.cpp
    kernel/threadman_thread.cpp
    kernel/threadman_sync.cpp
    kernel/iofilemgr.cpp
    kernel/modulemgr.cpp
    kernel/loadexec.cpp
    kernel/utils.cpp
    kernel/stdio.cpp
    kernel/kernel_library.cpp
    kernel/power_suspend.cpp
    kernel/umd.cpp
)
target_link_libraries(repsp_hle_kernel PUBLIC repsp_recomp)

target_sources(repsp_hle_ge PRIVATE
    ge/hle_ge.cpp
)
target_link_libraries(repsp_hle_ge PUBLIC repsp_recomp repsp_gpu)

target_sources(repsp_hle_display PRIVATE
    display/hle_display.cpp
)
target_link_libraries(repsp_hle_display PUBLIC repsp_recomp repsp_platform)

target_sources(repsp_hle_ctrl PRIVATE
    ctrl/hle_ctrl.cpp
)
target_link_libraries(repsp_hle_ctrl PUBLIC repsp_recomp repsp_platform)

target_sources(repsp_hle_dmac PRIVATE
    dmac/hle_dmac.cpp
)
target_link_libraries(repsp_hle_dmac PUBLIC repsp_recomp)

target_sources(repsp_hle_audio PRIVATE
    audio/hle_audio.cpp
)
target_link_libraries(repsp_hle_audio PUBLIC repsp_recomp)

target_sources(repsp_hle_sascore PRIVATE
    sascore/hle_sascore.cpp
)
target_link_libraries(repsp_hle_sascore PUBLIC repsp_recomp)

target_sources(repsp_hle_atrac PRIVATE
    atrac/hle_atrac.cpp
)
target_link_libraries(repsp_hle_atrac PUBLIC repsp_recomp repsp_codec)

target_sources(repsp_hle_mpeg PRIVATE
    mpeg/hle_mpeg.cpp
)
target_link_libraries(repsp_hle_mpeg PUBLIC repsp_recomp repsp_codec)
