set(_ffmpeg_root "${FFMPEG_ROOT}")
if(NOT _ffmpeg_root AND DEFINED ENV{FFMPEG_ROOT})
    set(_ffmpeg_root "$ENV{FFMPEG_ROOT}")
endif()
if(NOT _ffmpeg_root AND EXISTS "${PROJECT_SOURCE_DIR}/third_party/_ffmpeg/include/libavcodec/avcodec.h")
    set(_ffmpeg_root "${PROJECT_SOURCE_DIR}/third_party/_ffmpeg")
endif()

if(_ffmpeg_root AND NOT EXISTS "${_ffmpeg_root}/include/libavcodec/avcodec.h")
    file(GLOB _ffmpeg_nested LIST_DIRECTORIES true "${_ffmpeg_root}/ffmpeg-*")
    foreach(_cand IN LISTS _ffmpeg_nested)
        if(EXISTS "${_cand}/include/libavcodec/avcodec.h")
            set(_ffmpeg_root "${_cand}")
            break()
        endif()
    endforeach()
endif()

set(_ffmpeg_include_dirs "")
set(_ffmpeg_libs "")
set(_ffmpeg_shared_libs "")

if(_ffmpeg_root)
    find_path(_ffmpeg_inc_dir
        NAMES libavcodec/avcodec.h
        PATHS "${_ffmpeg_root}/include"
        NO_DEFAULT_PATH
    )
    find_library(_ffmpeg_avcodec_lib
        NAMES avcodec
        PATHS "${_ffmpeg_root}/lib" "${_ffmpeg_root}/bin"
        NO_DEFAULT_PATH
    )
    find_library(_ffmpeg_avutil_lib
        NAMES avutil
        PATHS "${_ffmpeg_root}/lib" "${_ffmpeg_root}/bin"
        NO_DEFAULT_PATH
    )
    if(NOT _ffmpeg_inc_dir OR NOT _ffmpeg_avcodec_lib OR NOT _ffmpeg_avutil_lib)
        message(FATAL_ERROR "REPSP_CODEC_FFMPEG=ON but FFmpeg headers/libraries were not found under FFMPEG_ROOT='${_ffmpeg_root}'")
    endif()
    set(_ffmpeg_include_dirs "${_ffmpeg_inc_dir}")
    set(_ffmpeg_libs "${_ffmpeg_avcodec_lib}" "${_ffmpeg_avutil_lib}")
    if(WIN32)
        file(GLOB _ffmpeg_dlls
            "${_ffmpeg_root}/bin/avcodec-*.dll"
            "${_ffmpeg_root}/bin/avutil-*.dll"
            "${_ffmpeg_root}/bin/swresample-*.dll"
        )
        set(_ffmpeg_shared_libs "${_ffmpeg_dlls}")
    else()
        file(GLOB _ffmpeg_sos
            "${_ffmpeg_root}/lib/libavcodec.so*"
            "${_ffmpeg_root}/lib/libavutil.so*"
            "${_ffmpeg_root}/lib/libswresample.so*"
        )
        set(_ffmpeg_shared_libs "${_ffmpeg_sos}")
    endif()
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG_AVCODEC QUIET libavcodec>=60)
        pkg_check_modules(FFMPEG_AVUTIL QUIET libavutil>=58)
    endif()
    if(NOT FFMPEG_AVCODEC_FOUND OR NOT FFMPEG_AVUTIL_FOUND)
        message(FATAL_ERROR "REPSP_CODEC_FFMPEG=ON requires FFMPEG_ROOT or pkg-config libavcodec>=60 and libavutil>=58")
    endif()
    set(_ffmpeg_include_dirs ${FFMPEG_AVCODEC_INCLUDE_DIRS} ${FFMPEG_AVUTIL_INCLUDE_DIRS})
    set(_ffmpeg_libs ${FFMPEG_AVCODEC_LINK_LIBRARIES} ${FFMPEG_AVUTIL_LINK_LIBRARIES})
    set(_ffmpeg_inc_dir "${FFMPEG_AVCODEC_INCLUDE_DIRS}")
endif()

if(EXISTS "${_ffmpeg_inc_dir}/libavcodec/version_major.h")
    file(STRINGS "${_ffmpeg_inc_dir}/libavcodec/version_major.h" _avcodec_major_line REGEX "#define[ \t]+LIBAVCODEC_VERSION_MAJOR")
    string(REGEX MATCH "[0-9]+" _avcodec_major "${_avcodec_major_line}")
    if(_avcodec_major AND _avcodec_major LESS 60)
        message(FATAL_ERROR "REPSP_CODEC_FFMPEG requires libavcodec >= 60 (found ${_avcodec_major})")
    endif()
endif()

set(REPSP_FFMPEG_SHARED_LIBS "${_ffmpeg_shared_libs}" CACHE INTERNAL "Runtime shared libraries required by repsp_codec_ffmpeg")

add_library(repsp_codec_ffmpeg SHARED
    atrac_ffmpeg.cpp
    avc_ffmpeg.cpp
    ffmpeg_plugin.cpp
    pixel_convert.cpp
)
add_library(repsp::codec_ffmpeg ALIAS repsp_codec_ffmpeg)

if(WIN32)
    set_target_properties(repsp_codec_ffmpeg PROPERTIES PREFIX "")
endif()

target_include_directories(repsp_codec_ffmpeg PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
)
target_include_directories(repsp_codec_ffmpeg SYSTEM PRIVATE
    ${_ffmpeg_include_dirs}
)
target_link_libraries(repsp_codec_ffmpeg PRIVATE ${_ffmpeg_libs})
repsp_apply_host_flags(repsp_codec_ffmpeg)
if(MSVC)
    target_compile_options(repsp_codec_ffmpeg PRIVATE /external:W0)
endif()
