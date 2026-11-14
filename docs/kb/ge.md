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
* `SIGNAL` / `FINISH` 인터럽트: `sceGeSetCallback`으로 등록한 콜백을 GE 명령 실행 시 커널 인터럽트 큐(`Scheduler::QueueInterrupt`)에 적재하고 안전점에서 16 KiB 인터럽트 스택으로 전달합니다.

---

# PSP Graphics Engine (GE) Display Lists and Software Rasterization

> Status labels: **Confirmed** (verified by synthetic `ge_probe` and `repsp_gpu`), **Inferred** (from public headers/external docs)

## 1. Display Lists and Stall Address (**Inferred**: PSPSDK `pspge.h`, uOFW; **Confirmed**: `ge_probe`)

The PSP GE executes 32-bit command words (`[31:24]` opcode, `[23:0]` 24-bit argument).

* `sceGeListEnQueue(list, stall, cbid, arg)`: Enqueues a display list. When `stall != 0`, execution stops upon reaching `stall` (`kGeListStalled = 3`).
* `sceGeListUpdateStallAddr(qid, stall)`: Advances the stall address and resumes execution.
* `sceGeListSync(qid, syncType)` / `sceGeDrawSync(syncType)`: Mode `1` peeks list/draw status (`0=Done .. 4=Paused`); mode `0` waits for completion.

## 2. Vertex, Texture, CLUT, and Framebuffer Conventions (**Inferred**: PSPSDK, psprecomp `5ca18fa`)

* `VTYPE` encodes texture, color, normal, position, weight, index, and `THROUGH` (bit 23, skipping transform/lighting).
* `FBP`/`FBW`/`PSM` configure the EDRAM (`0x04000000`) render target and pixel format (`0=565, 1=5551, 2=4444, 3=8888`).
* `TMODE` selects linear (`0`) vs PSP 16-byte × 8-row swizzled (`1`) layout; `T4`/`T8` textures resolve through `CLOAD`/`CMODE` CLUT entries.
* `SIGNAL`/`FINISH` queue guest callbacks via `Scheduler::QueueInterrupt` and deliver them on the 16 KiB kernel interrupt stack at safe points.
