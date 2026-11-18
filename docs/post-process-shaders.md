# 후처리 화면 셰이더 작성·사용 안내 (Post-Processing Screen Shaders)

`repsp_platform`의 OpenGL 3.3 Core 백엔드는 libretro 스타일의 GLSL(`.glsl`) 단일 패스 셰이더와 `.glslp` 다중 패스 프리셋을 지원합니다.

## 1. 내장 셰이더와 외부 셰이더 경로

* **내장 셰이더**: `none`, `crt`, `scanline`, `fsr`, `scalefx`, `anime4k`가 `repsp_platform` 라이브러리에 내장되어 있습니다.
* **외부 셰이더 디렉터리**: `ResolvePostShaderDirectory(executable_directory)`는 다음 순서로 `shaders/` 디렉터리를 찾습니다.
  1. `<executable_directory>/shaders`
  2. `<executable_directory>/../shaders`
  3. `<current_working_directory>/shaders`
  4. 모두 없으면 `<executable_directory>/shaders` 경로를 기본값으로 반환합니다.
* 외부 디렉터리에 `<id>.glsl` 또는 `<id>.glslp`를 두면 내장 셰이더와 같은 이름일 때 외부 파일이 우선합니다.

## 2. GLSL 3.3 Core / GLES 3.0 변환 규칙

libretro GLSL 셰이더는 보통 `#version 120` 문법(`attribute`, `varying`, `texture2D`, `gl_FragColor`)을 사용하지만 `repsp`는 **OpenGL 3.3 Core** 문맥에서 실행됩니다. `BuildPostShaderProgramSource()`가 셰이더 조립 단계에서 자동으로 다음 변환을 수행합니다.

1. 소스 맨 앞의 `#version ...` 지시문을 제거하고 `#version 330 core`(또는 GLES일 때 `#version 300 es` 및 `precision mediump float;`)를 삽입합니다.
2. 정점(`#define VERTEX`)과 프래그먼트(`#define FRAGMENT`) 단계, `#define PARAMETER_UNIFORM`을 정의합니다.
3. 호환 매크로를 주입합니다.
   * 정점 셰이더: `#define attribute in`, `#define varying out`, `#define texture2D texture`
   * 프래그먼트 셰이더: `#define varying in`, `#define texture2D texture`
4. `gl_FragColor`가 소스에서 사용된 경우에만 `out vec4 repsp_FragColor;`를 선언하고 식별자 토큰 단위로 `gl_FragColor`를 `repsp_FragColor`로 치환합니다. (`gl_FragColor`를 주석이나 다른 식별자 이름에 포함하지 않도록 토큰 단위로 처리합니다.)

### 작성 시 주의 사항

* 정점 속성 위치는 `VertexCoord = 0`, `COLOR = 1`, `TexCoord = 2`로 고정 바인딩됩니다.
* 프래그먼트 셰이더에서 `gl_FragColor` 대신 직접 `out vec4`를 선언하지 않아도 되지만, 사용자 정의 `out` 변수와 `gl_FragColor`를 섞어 쓰지 마십시오.

## 3. 크기 Uniform 계약: `LogicalSize` vs `TextureSize`

`repsp`는 PSP 기본 해상도(`480x272`)뿐 아니라 OpenGL 내부 렌더링 배율(`1x..8x`)을 지원합니다. 따라서 스캔라인이나 픽셀 경계 효과를 정확히 그리려면 실제 텍스처 픽셀 크기와 논리적 출력 해상도를 구분해야 합니다.

| Uniform | 의미 |
|---|---|
| `TextureSize` | 입력 텍스처의 실제 픽셀 크기 (예: 2x 렌더링 시 `960x544`) |
| `LogicalSize` | PSP 논리 프레임 크기 (예: `480x272`) |
| `InputSize` | 해당 패스 입력 텍스처의 유효 픽셀 크기 |
| `OutputSize` | 해당 패스 렌더 타깃(FBO 또는 최종 뷰포트)의 픽셀 크기 |
| `OrigTexture` | 첫 패스 입력(원본 게임 프레임) 샘플러 |
| `PassPrevNTexture` | 이전 패스 `N`의 출력 샘플러 (`N = 0, 1, ...`) |
| `FrameCount` | 누적 프레임 수 (`int`) |
| `FrameDirection` | 항상 `1` (`int`) |
| `MVPMatrix` | 정점 변환 행렬 (`mat4`, 단위 행렬) |

> [!TIP]
> 스캔라인처럼 PSP 원 해상도 줄 수(`272`)에 맞춰야 하는 셰이더는 `TextureSize` 대신 `uniform vec2 LogicalSize;`를 사용하십시오.

## 4. `#pragma parameter`

셰이더에 다음과 같이 파라미터를 선언하면 설정 메뉴에 슬라이더가 자동으로 나타납니다.

```glsl
#pragma parameter SCANLINE_STRENGTH "Scanline Strength" 0.35 0.0 1.0 0.05
```

* 형식: `#pragma parameter <NAME> "<Label>" <initial> <min> <max> <step>`
* 선언된 이름은 `uniform float <NAME>;`으로 전달됩니다.

## 5. `.glslp` 다중 패스 프리셋

`.glslp` 파일은 `key = value` 형식으로 패스 목록과 스케일/필터/형식을 지정합니다.

* `shaders = <N>`: 패스 수
* `shader0`, `shader1`, ...: `.glslp` 파일 기준 상대 경로의 `.glsl` 파일
* `filter_linear0`, ...: `true` 또는 `false`
* `wrap_mode0`, ...: `clamp_to_border`, `clamp_to_edge`, `repeat`, `mirrored_repeat`
* `float_framebuffer0`, ...: `true`이면 `RGBA16F`, 아니면 `RGBA8`
* `scale_type0` / `scale_type_x0` / `scale_type_y0`: `source`, `viewport`, `absolute`
* `scale0` / `scale_x0` / `scale_y0`: 배율 또는 절대 픽셀 크기

컴파일·링크·파일 읽기 오류가 발생하면 `none`으로 안전하게 폴백하고 마지막 오류 문자열을 `PostShaderControl::last_error()`에 기록합니다.

---

# Post-Processing Screen Shaders (English)

The `repsp_platform` OpenGL 3.3 Core backend supports libretro-style single-pass `.glsl` shaders and multi-pass `.glslp` presets.

## 1. Built-in Shaders and External Directory

* **Built-ins**: `none`, `crt`, `scanline`, `fsr`, `scalefx`, and `anime4k` are embedded in `repsp_platform`.
* **External shader directory**: `ResolvePostShaderDirectory(executable_directory)` checks `<executable_directory>/shaders`, `<executable_directory>/../shaders`, and `<current_working_directory>/shaders` in order, falling back to `<executable_directory>/shaders`.
* Placing `<id>.glsl` or `<id>.glslp` in the external directory overrides any built-in with the same ID.

## 2. GLSL 3.3 Core / GLES 3.0 Translation

`BuildPostShaderProgramSource()` translates libretro `#version 120` style shaders at assembly time:

1. Drops any leading `#version` line and prepends `#version 330 core` (or `#version 300 es` + `precision mediump float;` for GLES).
2. Defines `VERTEX` or `FRAGMENT` and `PARAMETER_UNIFORM`.
3. Injects `attribute`/`varying`/`texture2D` compatibility macros.
4. Token-replaces `gl_FragColor` with a declared `out vec4 repsp_FragColor;` when `gl_FragColor` is used.

Vertex attribute locations are bound to `VertexCoord = 0`, `COLOR = 1`, and `TexCoord = 2`.

## 3. Uniform Contract: `LogicalSize` vs `TextureSize`

* `TextureSize`: actual pixel dimensions of the input texture (e.g., `960x544` at 2x internal render scale).
* `LogicalSize`: logical PSP frame dimensions (e.g., `480x272`). Use `LogicalSize` for effects tied to PSP scanlines or pixel grid rather than internal render scale.
* Standard libretro uniforms: `InputSize`, `OutputSize`, `OrigTexture`, `PassPrevNTexture`, `FrameCount`, `FrameDirection`, `MVPMatrix`, plus any `#pragma parameter` uniforms.

## 4. `#pragma parameter` and `.glslp` Presets

* `#pragma parameter <NAME> "<Label>" <initial> <min> <max> <step>` exposes runtime float sliders in the UI.
* `.glslp` presets configure `shaders`, `shaderN`, `filter_linearN`, `wrap_modeN`, `float_framebufferN`, `scale_type*N`, and `scale*N`.
* Any load, compile, or link failure falls back cleanly to `none` and records an error string in `PostShaderControl::last_error()`.
