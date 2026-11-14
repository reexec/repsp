# 작업 로그: OpenGL 3.3 Core 렌더 백엔드 (issue #24)

## 요약

* `repsp_platform` (`include/repsp/platform/display.h`, `src/platform/sdl/sdl_platform.cpp`):
  * `DisplayConfig`에 `use_opengl`, `hidden`, `vsync` 필드를 추가하고, `DisplayBackend`에 `HasGlContext()`, `SubmitRenderJob()`, `WaitRenderIdle()`, `GetGlProcAddress()`, `PresentGlTexture()`를 추가했습니다.
  * `SdlDisplayBackend`의 UI 스레드에서 OpenGL 3.3 Core 문맥 생성(숨김 창 `SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN` 포함), 작업 큐 실행 및 대기, 대기 중인 present가 1개를 넘을 때의 흐름 제어, FBO 텍스처(`PresentGlTexture`)와 CPU 프레임 버퍼(`PresentRgba8`)의 풀스크린 쿼드 표시(알파 `1.0` 고정)를 구현했습니다. `dummy = true` 모드에서는 GL 문맥을 만들지 않습니다.
* `repsp_gpu` (`include/repsp/gpu/render_backend.h`, `src/gpu/texture_decode.{h,cpp}`, `src/gpu/gl_functions.h`, `src/gpu/gl_backend.cpp`, `src/gpu/render_backend.cpp`):
  * `ColorExpand`, `WrapCoord`, `TexelFetchWithCounter`, `DecodeTextureRgba8`, `ComputeTextureDataSizeBytes`를 `texture_decode.{h,cpp}`로 분리해 소프트웨어·OpenGL 백엔드가 공유하도록 정리했습니다.
  * SDL 헤더와 새 외부 로더 의존성 없이 `DisplayBackend::GetGlProcAddress`로 GL 3.3 Core 함수를 적재하는 `gl_functions.h`와 `kOpenGlBackend`를 구현했습니다.
  * 게스트 스레드에서 명령·정점·텍스처 디코드/해시 캐시(LRU 512개, CLUT 포함)를 기록해 UI 스레드로 제출하고, 대상 `GE_FBP`별 `RGBA8` + `DEPTH24_STENCIL8` FBO(`480*scale × 272*scale`, 배율 `1..8`)와 render-to-texture(대상 FBO 주소와 겹치는 텍스처 바인딩), 동일 상태 배치 병합, 근사 블렌딩 1회 경고·통계, CPU 쓰기 겹침 진단, `ReadGlTargetRgba8` readback을 구현했습니다.
  * `SelectRenderBackend("opengl")`는 GL 문맥이 없으면 `-1`을 반환하고 기존 백엔드(`software`)를 유지합니다.
* `repsp_hle_display` (`include/repsp/hle/display.h`, `src/hle/display/hle_display.cpp`, `CMakeLists.txt`):
  * `DisplayHleConfig`에 `use_opengl`, `vsync`, `internal_scale`을 추가했습니다.
  * `PresentActiveLocked`에서 `"opengl"` 백엔드가 활성이고 GE가 해당 프레임을 그렸으면 `PresentGlTarget`으로 표시하고, CPU가 직접 쓴 프레임은 `PresentRgba8`로 표시합니다. `REPSP_FRAME_DUMP`는 OpenGL 대상에서 배율 1로 readback해 BMP를 기록합니다.
* `gl_probe` (`tools/gl_probe/main.cpp`, `tools/CMakeLists.txt`):
  * GL 문맥 생성 실패 시 `SKIP_RETURN_CODE 77`로 건너뛰고, 가능할 때는 클리어, top-left fill rule, 깊이 테스트, CLUT4 텍스처+시저+알파 테스트, 컬러 테스트를 `software`와 `opengl`(배율 1)로 그려 픽셀 단위 비교(±1 허용, 불일치 0개), 배율 3(`1440×816`) readback, 종료 후 `SelectRenderBackend("opengl") == -1` 폴백을 검증합니다.

## 검증 결과

* Win64 Release (`Ninja`, MSVC 19.44): `ctest` **21/21 통과** (`gl_probe` 통과: `max_diff=0` 또는 `<=1`, `mismatches(>1)=0`).
* Win64 Debug (`Ninja`, MSVC 19.44): `ctest` **21/21 통과**.
* Linux 빌드/검증: 샌드박스 환경 제약으로 미실행(리뷰어 확인 필요).

## 문서 갱신

* `docs/kb/ge.md`: OpenGL 3.3 Core 백엔드, 공용 `texture_decode`, RTT, 근사 블렌딩 기록 추가.
* `ARCHITECTURE.md`: `repsp_gpu` 및 2.9절 GE/Display 계약(한·영) 갱신.

---

# Work Log: OpenGL 3.3 Core Render Backend (issue #24)

## Summary

* `repsp_platform` (`include/repsp/platform/display.h`, `src/platform/sdl/sdl_platform.cpp`):
  * Added `use_opengl`, `hidden`, `vsync` to `DisplayConfig`, and `HasGlContext()`, `SubmitRenderJob()`, `WaitRenderIdle()`, `GetGlProcAddress()`, `PresentGlTexture()` to `DisplayBackend`.
  * Implemented OpenGL 3.3 Core context creation on `SdlDisplayBackend`'s UI thread (including hidden-window GL mode via `SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN`), UI-thread render job queue and `WaitRenderIdle()`, backpressure when pending presents > 1, and fullscreen quad presentation with forced alpha `1.0` for both `PresentGlTexture` and `PresentRgba8`. `dummy = true` continues to skip GL context creation.
* `repsp_gpu` (`include/repsp/gpu/render_backend.h`, `src/gpu/texture_decode.{h,cpp}`, `src/gpu/gl_functions.h`, `src/gpu/gl_backend.cpp`, `src/gpu/render_backend.cpp`):
  * Extracted shared texture decoding (`ColorExpand`, `WrapCoord`, `TexelFetchWithCounter`, `DecodeTextureRgba8`, `ComputeTextureDataSizeBytes`) into `texture_decode.{h,cpp}`.
  * Added `gl_functions.h` (loading GL 3.3 Core procs via `DisplayBackend::GetGlProcAddress`, zero SDL headers, no external loader dependency) and `kOpenGlBackend`.
  * Recorded draw batches and CPU-decoded/hashed textures (512-entry LRU cache including CLUT) on the guest thread into self-contained flush payloads executed on the UI thread; added per-`GE_FBP` `RGBA8` + `DEPTH24_STENCIL8` FBOs (`480*scale × 272*scale`, scale `1..8`), render-to-texture overlap detection, state batching, one-time warnings and stats for approximated blends, CPU write overlap diagnostics, and `ReadGlTargetRgba8`.
  * `SelectRenderBackend("opengl")` returns `-1` and preserves the active backend when no GL context is available.
* `repsp_hle_display` (`include/repsp/hle/display.h`, `src/hle/display/hle_display.cpp`, `CMakeLists.txt`):
  * Added `use_opengl`, `vsync`, `internal_scale` to `DisplayHleConfig`.
  * Updated `PresentActiveLocked` to call `PresentGlTarget` for GL-drawn framebuffers and `PresentRgba8` for CPU-written framebuffers, and updated `REPSP_FRAME_DUMP` to read back GL targets at scale 1.
* `gl_probe` (`tools/gl_probe/main.cpp`, `tools/CMakeLists.txt`):
  * Added `gl_probe` (`SKIP_RETURN_CODE 77` if GL context creation fails) comparing `software` vs `opengl` at scale 1 across clear mode, top-left fill rule, depth test, CLUT4 texture + scissor + alpha test, and color test, plus scale 3 (`1440×816`) readback and post-shutdown fallback verification.

## Verification

* Win64 Release (`Ninja`, MSVC 19.44): `ctest` **21/21 passed** (`gl_probe` passed with `mismatches(>1)=0`).
* Win64 Debug (`Ninja`, MSVC 19.44): `ctest` **21/21 passed**.
* Linux build/test: not run in sandbox (for reviewer).

## Documentation Updates

* `docs/kb/ge.md`: documented OpenGL 3.3 Core backend, shared `texture_decode`, RTT, and approximated blend handling.
* `ARCHITECTURE.md`: updated `repsp_gpu` and §2.9 GE/Display contract (Korean and English).
