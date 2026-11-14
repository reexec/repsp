function(add_repsp_probe name)
    add_executable(${name} ${name}/main.cpp)
    target_link_libraries(${name} PRIVATE
        repsp_hle_ctrl
        repsp_hle_dmac
        repsp_hle_display
        repsp_hle_ge
        repsp_hle_kernel
        repsp_hle_audio
        repsp_hle_sascore
        repsp_hle_atrac
        repsp_hle_mpeg
        repsp_hle_utility
        repsp_codec
        repsp_gpu
        repsp_recomp
        repsp_module
        repsp_runtime
        repsp_platform
    )
    repsp_apply_host_flags(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_repsp_probe(memory_probe)
add_repsp_probe(module_probe)
add_repsp_probe(nid_probe)
add_repsp_probe(recomp_contract_probe)
add_repsp_probe(platform_probe)
add_repsp_probe(prx_probe)
add_repsp_probe(kernel_sched_probe)
add_repsp_probe(sysmem_probe)
add_repsp_probe(iofile_probe)
add_repsp_probe(ge_probe)
add_repsp_probe(display_probe)
add_repsp_probe(vblank_probe)
add_repsp_probe(ctrl_probe)
add_repsp_probe(dmac_probe)
add_repsp_probe(audio_probe)
add_repsp_probe(sas_probe)
add_repsp_probe(atrac_probe)
add_repsp_probe(mpeg_probe)
add_repsp_probe(utility_probe)
