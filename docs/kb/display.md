# PSP 디스플레이(`sceDisplay`)와 VBlank 동기화

> 상태 구분: **확인됨**(합성 프로브 `display_probe`로 검증), **추정**(PSPSDK `pspdisplay.h`, uOFW 기준)

## 1. 화면 규격 및 픽셀 형식 (**추정**: PSPSDK `pspdisplay.h`; **확인됨**: `display_probe`)

* PSP 네이티브 해상도는 `480 × 272`, 수직 주사율은 `59.94005966f` Hz(`sceDisplayGetFramePerSec`, `$f0` 반환)입니다.
* 프레임버퍼 픽셀 형식(`0..3`): `565`(16bpp), `5551`(16bpp), `4444`(16bpp), `8888`(32bpp, 알파 무시·불투명 표시).

## 2. `sceDisplaySetFrameBuf` 동기 모드 및 VBlank (**추정**: PSPSDK, uOFW; **확인됨**: `display_probe`)

* `sync == 0` (`PSP_DISPLAY_SETBUF_IMMEDIATE`): 활성 프레임버퍼를 즉시 갱신하고 화면에 표시(present/dump)합니다.
* `sync == 1` (`PSP_DISPLAY_SETBUF_NEXTFRAME`): 대기 버퍼로 기록하고 다음 VBlank 시점에 활성 버퍼로 승격·표시(latch)합니다. `SetFrameBuf`로 새 프레임버퍼가 latch된 때에만 `frames_presented`와 프레임 덤프가 올라가며, `topaddr == 0`이면 검은 화면을 보냅니다.
* `sceDisplayWaitVblankStart` / `sceDisplayWaitVblankStartCB`: 다음 VBlank 틱까지 현재 스레드를 대기(`WaitType::kCustom`)시키며, 호스트 VBlank 스레드가 주기적으로 대기자를 깨우고 `vblank_count`(`sceDisplayGetVcount`)를 증가시킵니다.

## 3. 호스트 백엔드 구조와 한계 (**미확정/현재 한계**)

* **단일 SDL UI 스레드 구조**: SDL 윈도우 생성·표시·이벤트 폴링은 한 스레드(`PollEvents`/present 경로)에 묶여 있습니다. Win32/Linux X11에서는 동작하지만, macOS(AppKit/Cocoa)처럼 메인 스레드에서만 UI/이벤트 루프를 허용하는 플랫폼에서는 별도 메인 스레드 펌프가 필요합니다.

---

# PSP Display (`sceDisplay`) and VBlank Synchronization

> Status labels: **Confirmed** (verified by synthetic `display_probe`), **Inferred** (PSPSDK `pspdisplay.h`, uOFW)

## 1. Resolution and Pixel Formats (**Inferred**: PSPSDK; **Confirmed**: `display_probe`)

* Native PSP resolution is `480 × 272` at `59.94005966f` Hz (`sceDisplayGetFramePerSec` returned in `$f0`).
* Supported framebuffer formats (`0..3`): `565`, `5551`, `4444`, `8888` (displayed opaque).

## 2. `sceDisplaySetFrameBuf` Sync Modes and VBlank (**Inferred**: PSPSDK, uOFW; **Confirmed**: `display_probe`)

* `sync == 0` (`PSP_DISPLAY_SETBUF_IMMEDIATE`): Immediately updates and presents the active framebuffer.
* `sync == 1` (`PSP_DISPLAY_SETBUF_NEXTFRAME`): Stages the framebuffer and promotes/presents it on the next VBlank tick. `frames_presented` and frame dumps advance only when `SetFrameBuf` latches a framebuffer (`topaddr == 0` presents a black screen), independent of `vblank_count`.
* `sceDisplayWaitVblankStart` / `sceDisplayWaitVblankStartCB` block the calling thread until the next VBlank tick wakes waiters and increments `vblank_count` (`sceDisplayGetVcount`).

## 3. Host Backend Threading Limitation (**Unconfirmed / Current Limitation**)

* **Single SDL UI thread model**: SDL windowing, presentation, and event polling are confined to one thread. This works on Win32 and Linux, but platforms such as macOS (where Cocoa requires UI events on thread 0) will need main-thread event pumping.
