target_sources(repsp_codec PRIVATE
    codec.cpp
    loader.cpp
)
target_link_libraries(repsp_codec PUBLIC repsp_runtime repsp_platform)
