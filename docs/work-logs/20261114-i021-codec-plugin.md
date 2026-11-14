# Work Log: Optional FFmpeg Codec Plugin (`i021-codec-plugin`)

## 작업 요약 (한국어)

### 1. 구현 내용
* **동적 라이브러리 로더 (`repsp_platform`)**:
  * [`include/repsp/platform/dynlib.h`](../../include/repsp/platform/dynlib.h), [`src/platform/win32/dynlib_win32.cpp`](../../src/platform/win32/dynlib_win32.cpp), [`src/platform/linux/dynlib_linux.cpp`](../../src/platform/linux/dynlib_linux.cpp) 추가 (`LoadLibraryW`/`GetProcAddress`/`FreeLibrary` 및 `dlopen`/`dlsym`/`dlclose`).
  * [`src/platform/sdl/sdl_platform.cpp`](../../src/platform/sdl/sdl_platform.cpp)에 `REPSP_AUDIO_DUMP` 환경 변수 기반 WAV 캡처 추가(미설정 시 오버헤드 없음).
* **코덱 플러그인 ABI·로더 및 스트림 컨텍스트 (`repsp_codec`, `hle_atrac`, `hle_mpeg`)**:
  * 순수 C 플러그인 ABI [`include/repsp/codec/plugin.h`](../../include/repsp/codec/plugin.h) (`RepspCodecPluginV1`, `REPSP_CODEC_PLUGIN_ABI_VERSION=1`) 및 로더 [`include/repsp/codec/loader.h`](../../include/repsp/codec/loader.h) / [`src/codec/loader.cpp`](../../src/codec/loader.cpp) 구현.
  * `AtracFrameInfo`에 `extradata`를 추가하고 `AtracDecoder`/`AvcDecoder`에 선택적 스트림별 컨텍스트(`CreateStreamContext`, `DestroyStreamContext`, `ResetStreamContext`/`Flush`)를 추가.
  * `hle_atrac`는 RIFF `fmt ` 청크의 `extradata`를 전달하고 `ResetPlayPosition`·루프 시 컨텍스트를 초기화하며, `hle_mpeg`는 PSMF ATRAC3plus 8바이트 헤더(`0F D0 ...`) 제거 및 스트림 컨텍스트 수명을 관리.
* **선택적 FFmpeg 플러그인 (`REPSP_CODEC_FFMPEG`, 기본 `OFF`)**:
  * [`src/codec_ffmpeg/`](../../src/codec_ffmpeg/CMakeLists.txt) (`atrac_ffmpeg.cpp`, `avc_ffmpeg.cpp`, `pixel_convert.cpp`, `ffmpeg_plugin.cpp`) 추가: `libavcodec >= 60` LGPL 공유 빌드에 동적 링크하고, BT.601 제한 범위 YUV420p → PSP 8888/5650/5551/4444 변환과 planar float → s16le 포화 변환을 `libswscale`/`libswresample` 없이 직접 수행.
  * [`scripts/fetch_ffmpeg.py`](../../scripts/fetch_ffmpeg.py) 추가(고정 BtbN `lgpl-shared` 빌드 내려받기, SHA-256 및 LGPL 라이선스 검증, GPL 빌드 거부).
* **프로브**: `codec_loader_probe`(누락/비라이브러리/ABI 불일치 가짜 플러그인 검증) 및 `codec_ffmpeg_probe`(합성 16×16 H.264 `I_PCM` IDR 프레임 디코딩·색상 변환·flush, ATRAC 컨텍스트·오류 처리 검증) 추가.

### 2. 검증 결과
* Win64 Release (`REPSP_CODEC_FFMPEG=OFF`): Ninja 빌드 성공, CTest **20/20 통과**.
* Win64 Release (`REPSP_CODEC_FFMPEG=ON`, `FFMPEG_ROOT` 지정): Ninja 빌드 성공, CTest **21/21 통과** (`codec_loader_probe`, `codec_ffmpeg_probe` 포함).
* `scripts/fetch_ffmpeg.py`: 로컬 `file://` LGPL/GPL 모의 zip으로 SHA-256 검증, 캐시 건너뛰기, GPL 거부 동작 통과.
* Linux x64 빌드/검증: 샌드박스 환경 제약으로 미실행(리뷰어 확인 필요).

---

## Work Summary (English)

### 1. Changes
* **Dynamic library loader (`repsp_platform`)**: Added `DynamicLibrary` for Win32 and Linux, plus zero-cost-when-unset `REPSP_AUDIO_DUMP` WAV capture in `SdlAudioBackend`.
* **Codec plugin ABI, loader, and per-stream contexts (`repsp_codec`, `hle_atrac`, `hle_mpeg`)**: Added pure-C `plugin.h` (`RepspCodecPluginV1`), `LoadCodecPlugin`, `AtracFrameInfo::extradata`, and optional per-stream context lifecycle on `AtracDecoder` / `AvcDecoder`. Updated `hle_atrac` (passes `fmt ` extradata, resets context on loop/seek) and `hle_mpeg` (strips 8-byte PSMF ATRAC3plus header, manages per-instance AVC/ATRAC contexts).
* **Optional FFmpeg plugin (`REPSP_CODEC_FFMPEG=OFF` by default)**: Added `repsp_codec_ffmpeg` linking dynamically against `libavcodec >= 60` LGPL shared builds, implementing BT.601 YUV420p→PSP RGB and float-planar→s16le conversion directly without `libswscale`/`libswresample`. Added `scripts/fetch_ffmpeg.py` with SHA-256 and LGPL license verification.
* **Probes**: Added `codec_loader_probe` (missing/invalid/ABI-mismatch fake plugin) and `codec_ffmpeg_probe` (synthetic 16x16 H.264 `I_PCM` IDR decode, flush, ATRAC error/context checks).

### 2. Verification
* Win64 Release (`REPSP_CODEC_FFMPEG=OFF`): Ninja build succeeded, CTest **20/20 passed**.
* Win64 Release (`REPSP_CODEC_FFMPEG=ON` with `FFMPEG_ROOT`): Ninja build succeeded, CTest **21/21 passed**.
* `scripts/fetch_ffmpeg.py`: Verified SHA-256 check, cache skip, and GPL rejection with local `file://` mock archives.
* Linux x64 build/test: Not run in sandbox (deferred to reviewer).
