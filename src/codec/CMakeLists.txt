target_sources(repsp_codec PRIVATE
    codec.cpp
)
target_link_libraries(repsp_codec PUBLIC repsp_runtime)
