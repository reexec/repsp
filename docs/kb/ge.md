# PSP Graphics Engine (GE) 디스플레이 리스트와 소프트웨어 래스터화

> 상태 구분: **확인됨**(합성 프로브 `ge_probe` 및 `repsp_gpu` 구현으로 검증), **추정**(외부 문서/공개 헤더 기준)

## 1. 디스플레이 리스트와 stall 주소 (**추정**: PSPSDK `pspge.h`, uOFW; **확인됨**: `ge_probe`)

PSP GE는 32비트 명령어 워드(`[31:24]` opcode, `[23:0]` 24비트 인자)의 스트림으로 동작합니다.

* `sceGeListEnQueue(list, stall, cbid, arg)`: 디스플레이 리스트를 큐에 넣고 실행합니다. `stall != 0`이면 리스트 포인터가 `stall`에 도달할 때 실행이 중단(`kGeListStalled = 3`)됩니다.
* `sceGeListUpdateStallAddr(qid, stall)`: 중단된 리스트의 `stall` 주소를 갱신하고 재개합니다.
* `sceGeListSync(qid, syncType)` / `sceGeDrawSync(syncType)`: `syncType == 1`이면 현재 상태(`0=Done, 1=Queued, 2=Running, 3=Stalled, 4=Paused`)를 즉시 반환하고, `syncType == 0`이면 완료를 대기합니다.

## 2. 정점, 텍스처, CLUT, 프레임버퍼 규약 (**추정**: PSPSDK `pspgu.h`/`pspge.h`, psprecomp `5ca18fa`)

* 정점 형식(`VTYPE`): `TEX`(2b), `COL`(3b), `NRM`(2b), `POS`(2b), `WEIGHT`(2b), `INDEX`(2b), `THROUGH`(1b, 비트 23). `THROUGH=1`이면 정점 좌표가 이미 화면 좌표로 간주되어 변환·조명 단계를 건너뜁니다.
* 프레임버퍼(`FBP`, `FBW`, `PSM`): EDRAM(`0x04000000`, 2 MiB) 내 오프셋과 스트라이드(픽셀 단위, 보통 512), 픽셀 형식(`0=565, 1=5551, 2=4444, 3=8888`).
* 텍스처(`TBP0`, `TBW0`, `TSIZE0`, `TMODE`, `TPSM`, `TFUNC`): 선형(`TMODE=0`)과 PSP 16바이트×8행 스위즐(`TMODE=1`)을 지원하며, 인덱스 텍스처(`T4`/`T8`)는 `CBP`/`CBPH`/`CLOAD`/`CMODE`로 로드한 CLUT를 참조합니다.
* `SIGNAL` / `FINISH` 인터럽트: `sceGeSetCallback`으로 등록한 콜백을 GE 명령 실행 시 커널 인터럽트 큐(`Scheduler::QueueInterrupt`)에 적재하고 안전점에서 16 KiB 인터럽트 스택으로 전달합니다. `SIGNAL`과 `FINISH`는 각각 바로 뒤의 `END` 명령과 한 쌍을 이루며, `SIGNAL`+`END`는 리스트를 끝내지 않습니다.

## 3. 주소 계산 (`BASE`, `OFFSET_ADDR`, `ORIGIN_ADDR`) (**추정**: [PSPSDK `pspgu`](https://github.com/pspdev/pspsdk), [psdevwiki GE](https://www.psdevwiki.com/psp/GE); **확인됨**: `ge_probe`)

GE 명령의 포인터 인자는 24비트이므로 32비트 주소를 구성하기 위해 `base`와 `offset` 레지스터를 함께 씁니다.

* `BASE` (`0x10`): 인자의 상위 비트(`[23:16]`)를 8비트 왼쪽 시프트해 `base` 레지스터(`(arg & 0xFF0000) << 8`, bits 24–31)를 설정합니다.
* `OFFSET_ADDR` (`0x13`): 24비트 인자를 8비트 왼쪽 시프트(`arg << 8`)해 `offset` 레지스터를 설정합니다.
* `ORIGIN_ADDR` (`0x14`): 현재 `ORIGIN_ADDR` 명령어 자신의 주소를 `offset` 레지스터에 저장합니다.
* `JUMP` (`0x08`), `CALL` (`0x0A`), `VADDR` (`0x01`), `IADDR` (`0x02`): 공통 주소 식은 `(base | (arg & 0x00FFFFFF)) + offset`이며, `JUMP`/`CALL`은 4바이트 정렬(`& ~3`)합니다. `CALL` 스택은 리스트 슬롯에 저장되며 넘침·빈 스택 `RET`는 정지 보고합니다.

## 4. 미확정 및 미구현 사항

* **즉시 실행과 `SIGNAL` 핸들러 실행 시점 (미확정/현재 한계)**: 현재 GE 해석기는 `sceGeListEnQueue` 시점에 동기·즉시 실행되므로, `SIGNAL` 인터럽트 핸들러는 리스트 전체가 끝난 뒤 스케줄러 안전점에서 실행됩니다(behavior `1` 등의 일시정지 후 재개 의미와 다름).
* **`kPrimTriangles` 픽셀 중심 + top-left fill rule (`render_backend.cpp`, 확인됨)**: `SwTri`는 픽셀 중심 `(x + 0.5, y + 0.5)`을 표본으로 삼고, 와인딩 정규화(`area < 0`이면 `v1`↔`v2` 스왑) 후 `IsTopLeftEdge`(`dy == 0 && dx > 0` 또는 `dy > 0`)로 배타적 아래쪽·오른쪽 경계를 제외합니다. 이를 통해 2-삼각형 사각형 경계 밖의 1픽셀 아틀라스 패딩(`#FF00FF`) 샘플링을 방지합니다(`ge_probe` 검증).
* **Color Test (`CTE` `0x27`, `CTEST` `0xD8`, `CREF` `0xD9`, `CMSK` `0xDA`, 추정: [PSPSDK `pspgu.h`](https://github.com/pspdev/pspsdk); 확인됨: `ge_probe`)**: `CTE`가 활성화되면 텍스처 결합 후, 알파 테스트 전에 `(c & 0x00FFFFFF & mask)`와 `(ref & mask)`를 `0=NEVER`, `1=ALWAYS`, `2=EQUAL`, `3=NOTEQUAL`로 비교해 불합격 프래그먼트를 폐기합니다.
* **480×272 고정 클립 (미구현)**: 소프트웨어 래스터라이저는 `0..479 × 0..271`로 고정 클립되어 화면 밖(off-screen) 렌더 타깃이나 480×272 초과 크기의 타깃을 그리지 않습니다.
* **미지원 primitive 및 그래픽 기능 (미구현)**:
  * primitive: `points`, `lines`, `line-strip`, `bezier`, `spline`은 그리지 않고 `unsupported` 통계로만 셉니다.
  * 파이프라인 기능: culling, stencil test/op, color mask, fog, 텍스처 필터링(bilinear) 및 mipmap, vertex skinning 및 morphing은 아직 구현되지 않았습니다.

---

# PSP Graphics Engine (GE) Display Lists and Software Rasterization

> Status labels: **Confirmed** (verified by synthetic `ge_probe` and `repsp_gpu`), **Inferred** (from public headers/external docs)

## 1. Display Lists and Stall Address (**Inferred**: PSPSDK `pspge.h`, uOFW; **Confirmed**: `ge_probe`)

The PSP GE executes 32-bit command words (`[31:24]` opcode, `[23:0]` 24-bit argument).

* `sceGeListEnQueue(list, stall, cbid, arg)`: Enqueues a display list. When `stall != 0`, execution stops upon reaching `stall` (`kGeListStalled = 3`). If an earlier list is stalled or paused, subsequent lists remain `kGeListQueued` until earlier lists finish.
* `sceGeListUpdateStallAddr(qid, stall)`: Advances the stall address and resumes execution.
* `sceGeListSync(qid, syncType)` / `sceGeDrawSync(syncType)`: Mode `1` peeks list/draw status (`0=Done .. 4=Paused`); mode `0` blocks via the scheduler until the list (or all lists) completes.

## 2. Vertex, Texture, CLUT, and Framebuffer Conventions (**Inferred**: PSPSDK, psprecomp `5ca18fa`)

* `VTYPE` encodes texture, color, normal, position, weight, index, and `THROUGH` (bit 23, skipping transform/lighting).
* `FBP`/`FBW`/`PSM` configure the EDRAM (`0x04000000`, 2 MiB) render target and pixel format (`0=565, 1=5551, 2=4444, 3=8888`); writes outside the 2 MiB VRAM bounds are dropped and counted.
* `TMODE` selects linear (`0`) vs PSP 16-byte × 8-row swizzled (`1`) layout; `T4`/`T8` textures resolve through `CLOAD`/`CMODE` CLUT entries.
* `SIGNAL`/`FINISH` queue guest callbacks via `Scheduler::QueueInterrupt` and deliver them on the 16 KiB kernel interrupt stack at safe points. `SIGNAL` and `FINISH` each pair with the `END` command that follows; `SIGNAL`+`END` does not terminate the list.

## 3. Address Arithmetic (`BASE`, `OFFSET_ADDR`, `ORIGIN_ADDR`) (**Inferred**: [PSPSDK `pspgu`](https://github.com/pspdev/pspsdk), [psdevwiki GE](https://www.psdevwiki.com/psp/GE); **Confirmed**: `ge_probe`)

* `BASE` (`0x10`): sets bits 24–31 of the base register (`(arg & 0xFF0000) << 8`).
* `OFFSET_ADDR` (`0x13`): sets the offset register (`arg << 8`).
* `ORIGIN_ADDR` (`0x14`): sets the offset register to the address of the `ORIGIN_ADDR` command itself.
* `JUMP` (`0x08`), `CALL` (`0x0A`), `VADDR` (`0x01`), `IADDR` (`0x02`): compute `(base | (arg & 0x00FFFFFF)) + offset` (`& ~3` for `JUMP`/`CALL`). The `CALL` stack lives in `QueueSlot`, and overflow or `RET` on an empty stack triggers a stop report.

## 4. Unconfirmed and Unimplemented Areas

* **Immediate execution vs `SIGNAL` handlers (unconfirmed / current limitation)**: Because the interpreter executes synchronously at enqueue time, `SIGNAL` handlers run at safe points after the list returns rather than pausing mid-list (unlike hardware behavior `1` pause/resume semantics).
* **`kPrimTriangles` pixel-center + top-left fill rule (`render_backend.cpp`, confirmed)**: `SwTri` samples pixel centers `(x + 0.5, y + 0.5)`, normalizes negative-area winding (`area < 0` swaps `v1`/`v2`), and applies `IsTopLeftEdge` (`dy == 0 && dx > 0` or `dy > 0`) to exclude bottom/right exclusive edges, preventing 1-pixel spills into adjacent atlas padding (`#FF00FF`), verified by `ge_probe`.
* **Color Test (`CTE` `0x27`, `CTEST` `0xD8`, `CREF` `0xD9`, `CMSK` `0xDA`, inferred: [PSPSDK `pspgu.h`](https://github.com/pspdev/pspsdk); confirmed: `ge_probe`)**: When `CTE` is enabled, `ColorPass` compares `(c & 0x00FFFFFF & mask)` against `(ref & mask)` using `0=NEVER`, `1=ALWAYS`, `2=EQUAL`, or `3=NOTEQUAL` after texture combining and before alpha testing, discarding failing fragments.
* **Fixed 480×272 clip (unimplemented)**: The software rasterizer clips to `0..479 × 0..271` and does not support off-screen render targets outside those bounds.
* **Unimplemented primitives and features (unimplemented)**: `bezier` and `spline` are counted as `unsupported` without drawing; culling, stencil, colour mask, fog, texture filtering/mipmaps, and skinning/morphing are not yet implemented. (`points`, `lines`, and `line-strip` are drawn by the OpenGL backend.)

## 5. OpenGL 3.3 Core Render Backend (`gl_backend.cpp`, Confirmed: `gl_probe`)

* **UI-thread execution (`DisplayBackend::SubmitRenderJob`, `WaitRenderIdle`)**: Guest threads record `GlDraw` batches and decode/hash textures on the CPU into self-contained flush payloads; all OpenGL 3.3 Core calls run strictly on `SdlDisplayBackend`'s UI thread.
* **FBO targets & internal resolution (`SetInternalScale`)**: Each `GE_FBP` target owns an `RGBA8` + `DEPTH24_STENCIL8` FBO sized `480*scale × 272*scale` (scale `1..8`). Presentation draws the FBO texture onto the default framebuffer with alpha forced to `1.0`.
* **Shared texture decoding & RTT**: `src/gpu/texture_decode.cpp` provides `TexelFetchWithCounter` and `DecodeTextureRgba8` for both `software` and `opengl`. When `tex_addr` overlaps a drawn FBO target, the OpenGL backend binds that target's color texture directly (render-to-texture) instead of decoding guest VRAM.
* **Approximated blend modes**: PSP 2× alpha factors (`6..9`), conflicting non-trivial `SFIX`/`DFIX`, and abs-diff (`op=5`) are approximated in standard GL blend equations, logged once, and counted in `OpenGlStats::approximated_blends`.
