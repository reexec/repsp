function(add_repsp_probe name)
    add_executable(${name} ${name}/main.cpp)
    target_link_libraries(${name} PRIVATE
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
