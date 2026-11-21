# 작업 로그: 두 번째 타이틀 부팅 경로 HLE 측정 반복 (issue #67)

* 이슈: [#67](https://github.com/reexec/repsp/issues/67) (소비자 요구: [reexec/reDMP2#3](https://github.com/reexec/reDMP2/issues/3))
* 설계: [20261121-i067-boot-path-hle.md](../design/20261121-i067-boot-path-hle.md)
* 작업 지시: [20261121-i067-boot-path-hle.md](../work-orders/20261121-i067-boot-path-hle.md)
* 브랜치: `i67-boot-path-hle`

---

## 1. 단계 1: NID 이름 확인 (`tools/nid_hash.py`, `src/runtime/nid_names.cpp`)

* Python 표준 라이브러리 전용 검증 스크립트 `tools/nid_hash.py`를 추가했습니다(단일/복수 이름 → SHA-1 앞 4바이트 리틀 엔디언 NID 계산, `--match` / `--candidates` 일괄 대조).
* 공개 PSPSDK / uOFW / psprecomp 함수 이름 후보에 대해 SHA-1을 대조하여 소비자 미구현 NID 88개 중 **87개**의 이름을 확인하고, 기존에 없던 **67개** 이름을 `src/runtime/nid_names.cpp`에 추가했습니다(`nid_names.cpp` 총 항목: `216` → `283`).
* **미확인 NID (1개)**:
  * `ModuleMgrForUser:0x8F2DF740` (공개 후보 대조로 일치하지 않아 `nid_names.cpp`에 넣지 않고 기록만 유지).

---

## 2. 단계 2: 부팅 경로 측정 반복 (설계 §1 멈춤 기준 도달)

| 반복 | 정지 NID / 사유 | 구현 내용 (출처) | 재실행 결과 |
|---:|---|---|---|
| 1 | `(b) Unimplemented kernel NID`: `SysMemUserForUser` `0x7591C7DB` (`sceKernelSetCompiledSdkVersion`) | `src/hle/kernel/sysmem.cpp`에 `sceKernelSetCompiledSdkVersion` (`0x7591C7DB`), `sceKernelSetCompilerVersion` (`0xF77D77CB`), `sceKernelMaxFreeMemSize` (`0xA291F107`), `sceKernelTotalFreeMemSize` (`0xF919F628`) 추가(출처: uOFW / psprecomp `src/hle/sysmem.c`, **추정** → 실행·probe로 **확인됨**). | 소비자 패치(`0007-exec-scan-code-pointers.patch`) 적용 후 `user_main` 진입 및 `UserSbrk` 할당 통과 → `ModuleMgrForUser:sceKernelGetModuleIdByAddress` (`0xD8B73127`)에서 정지. |
| 2 | `(b) Unimplemented kernel NID`: `ModuleMgrForUser` `0xD8B73127` (`sceKernelGetModuleIdByAddress`) | `src/hle/kernel/modulemgr.cpp`에 메인 모듈 `kModule` UID 등록 및 `sceKernelGetModuleIdByAddress` (`0xD8B73127`), `sceKernelGetModuleId` (`0xF0A26395`) 구현(`kSceKernelErrorUnknownModule = 0x8002013E`, 출처: PSPSDK / uOFW). | `sceKernelGetModuleIdByAddress` 1회 및 `sceKernelCpuSuspendIntr`/`ResumeIntr` 통과 → `sceUtility:sceUtilityGetSystemParamInt` (`0xA5DA2406`)에서 정지. |
| 3 | `(a) Unimplemented non-kernel NID`: `sceUtility` `0xA5DA2406` (`sceUtilityGetSystemParamInt`) | `src/hle/utility/hle_utility.cpp`에 `sceUtilityGetSystemParamInt` (`0xA5DA2406`, PSPSDK `psputility_sysparam.h` ID 2~9 및 오류 `0x80110103`)과 모듈 로드/언로드 6종(`sceUtilityLoadModule`/`UnloadModule`, `LoadAvModule`/`UnloadAvModule`, `LoadNetModule`/`UnloadNetModule` — HLE 상주 성공 반환, 설계 §2) 구현. | `sceUtilityGetSystemParamInt(id=8)` 1회 통과 → `sceImpose:sceImposeSetLanguageMode` (`0x36AA6E91`)에서 정지. |
| 4 | `(a) Unimplemented non-kernel NID`: `sceImpose` `0x36AA6E91` (`sceImposeSetLanguageMode`) | `src/hle/kernel/impose.cpp`에 `sceImposeSetLanguageMode` (`0x36AA6E91`)과 `sceImposeSetUMDPopup` (`0x72189C48`) 추가(출처: PSPSDK `pspimpose_driver.h` / uOFW `impose`). | 소비자 패치(`0008-exec-scan-outside-text-bodies.patch`) 적용 후 C++ 정적 초기화, `sceDisplaySetMode`(2회), `sceGe`(2 리스트, 289 명령, 2 finish), `scePowerRegisterCallback`, `sceUmdActivate`/`GetDriveStat`, `sceUtilityLoadAvModule`(3회) 통과 → `ThreadManForUser:sceKernelCreateVpl` (`0x56C039B5`)에서 정지. |
| 5 | `(b) Unimplemented kernel NID`: `ThreadManForUser` `0x56C039B5` (`sceKernelCreateVpl`) | `src/hle/kernel/threadman_vpl.cpp`에 VPL 커널 객체(`KernelObjectType::kVpl`, `kSceKernelErrorUnknownVplid = 0x8002019E`, 출처: PSPSDK `pspthreadman.h` / uOFW `vpl.c`) 및 7개 함수(`CreateVpl`, `DeleteVpl`, `AllocateVpl`, `AllocateVplCB`, `TryAllocateVpl`, `FreeVpl`, `ReferVplStatus`) 구현(8바이트 정렬, 인접 자유 블록 병합, 스케줄러 대기/깨우기 핸드오프). | VPL 2개(`VOLATILE` 16 MiB, `GLOBAL` 160 KiB) 생성, UMD 자산 파일 2개 열기, 오디오 스레드(`sound_poll_thread`) 기동 및 4채널 예약·SAS Core 256프레임 처리, **첫 프레임 표시(`Present: 1 frames`, `480x272`)** 후 600 VBlank 한도 도달 정상 종료(`Exit code: 0`). |

### 최종 정지 지점 (설계 §1 완료 조건 도달)
* **정지 분류**: `(e) VBlank limit reached (--max-vblanks)` (`Exit code: 0`)
* **표시 / 실행 요약**: `Present: 1 frames` (`480x272`, `stride=512`, `fmt=3`, `vblanks=600`), `Ctrl: cycle=0 mode=1 produced=600 read=0`, `GE: 2 lists, 289 commands, 2 finishes`, `Audio: reserved=4 output_calls=1 frames_written=256`, `SAS: instances=1 calls=1 frames=256`, 게스트 스레드 3개(`module_start`, `user_main`, `sound_poll_thread`), 열린 파일 2개.
* 설계 §1 멈춤 기준("첫 프레임이 표시되거나(`Present` > 0), 미구현 NID가 아닌 이유로 멈추면 이 issue를 끝내고 그 지점을 보고한다")에 도달하여 issue #67을 완료합니다. 네트워크 모듈(`repsp_hle_net`)과 기타 미호출 함수는 호출되지 않아 후속 이슈로 남깁니다.

---

## 3. 정적 임포트 대조 변화 (소비자 측)

* `repsp` 등록 HLE NID 수: `146` → `150` → `161` → **`168`** (`+22`)
* `nid_names.cpp` 공개 이름 수: `216` → **`283`** (`+67`)
* 소비자 정적 커버리지: **`141 / 229` (미구현 88)** → **`158 / 229` (미구현 71)** (`SysMemUserForUser` 7/8, `ModuleMgrForUser` 4/5, `ThreadManForUser` 31/33, `sceUtility` 13/17, `sceImpose` 2/2)

---

## 4. probe 및 단독 빌드 검증

* `tools/sysmem_probe/main.cpp`: `SysMem` 4종, `ModuleMgr` 2종(`GetModuleId`, `GetModuleIdByAddress`), `sceImpose` 2종 검증 추가.
* `tools/utility_probe/main.cpp`: `sceUtilityGetSystemParamInt` 및 `sceUtilityLoad*/Unload*` 6종 검증 추가.
* `tools/kernel_sched_probe/main.cpp`: VPL 생성·할당·소진 실패(`NO_MEMORY`/`WAIT_TIMEOUT`)·자유 블록 병합·대기 스레드 깨우기 핸드오프·삭제 검증(테스트 #17) 추가.
* `ARCHITECTURE.md`: `repsp_hle_kernel` (`80`개), `repsp_hle_utility` (`15`개) 갱신.
* **Win64 Debug (`win64-debug`)**: `repsp` 타깃 경고 **0건**(서드파티 `imgui.vcxproj` `C4819` 2건 동일), CTest **23/23 통과**.
* **Win64 Release (`win64-release`)**: `repsp` 타깃 경고 **0건**(동일 `C4819` 2건), CTest **23/23 통과**.

---

# Work Log: Boot-Path HLE Measurement Loop (issue #67)

* Issue: [#67](https://github.com/reexec/repsp/issues/67) (consumer request: [reexec/reDMP2#3](https://github.com/reexec/reDMP2/issues/3))
* Design: [20261121-i067-boot-path-hle.md](../design/20261121-i067-boot-path-hle.md)
* Work order: [20261121-i067-boot-path-hle.md](../work-orders/20261121-i067-boot-path-hle.md)
* Branch: `i67-boot-path-hle`

---

## 1. Step 1: NID Name Verification (`tools/nid_hash.py`, `src/runtime/nid_names.cpp`)

* Added `tools/nid_hash.py` (Python standard library only) to compute PSP NIDs (`SHA-1(name)[0..4]` LE) and match NID lists against candidate names.
* Verified **87 of 88** previously unregistered consumer NIDs against public PSPSDK / uOFW / psprecomp names and added **67** newly verified entries to `src/runtime/nid_names.cpp` (`216` → `283` total known names).
* **Unconfirmed NID (1)**: `ModuleMgrForUser:0x8F2DF740` (left out of `nid_names.cpp`).

## 2. Step 2: Boot-Path Loop (Design §1 Stopping Rule Reached)

* **Iteration 1 (`SysMemUserForUser` `0x7591C7DB`)**: Implemented `sceKernelSetCompiledSdkVersion`, `sceKernelSetCompilerVersion`, `sceKernelMaxFreeMemSize`, `sceKernelTotalFreeMemSize`. After consumer patch `0007`, `user_main` entered and allocated `UserSbrk`.
* **Iteration 2 (`ModuleMgrForUser` `0xD8B73127`)**: Implemented main module UID registration, `sceKernelGetModuleIdByAddress`, and `sceKernelGetModuleId` (`kSceKernelErrorUnknownModule = 0x8002013E`). Advanced past CRT/newlib init to `sceUtilityGetSystemParamInt`.
* **Iteration 3 (`sceUtility` `0xA5DA2406`)**: Implemented `sceUtilityGetSystemParamInt` (PSPSDK IDs 2..9, error `0x80110103`) and module load/unload helpers (`sceUtilityLoad/Unload{,Av,Net}Module`). Advanced to `sceImposeSetLanguageMode`.
* **Iteration 4 (`sceImpose` `0x36AA6E91`)**: Implemented `sceImposeSetLanguageMode` and `sceImposeSetUMDPopup` in `src/hle/kernel/impose.cpp`. After consumer patch `0008`, passed C++ static init, `sceDisplaySetMode`, `sceGe` (2 lists, 289 commands), `scePowerRegisterCallback`, `sceUmd*`, and `sceUtilityLoadAvModule` (3), stopping at `ThreadManForUser:sceKernelCreateVpl`.
* **Iteration 5 (`ThreadManForUser` `0x56C039B5`)**: Implemented the VPL kernel object (`KernelObjectType::kVpl`, `kSceKernelErrorUnknownVplid = 0x8002019E`, 8-byte alignment, span coalescing, scheduler wait/wake handoff) and 7 functions (`CreateVpl`, `DeleteVpl`, `AllocateVpl`, `AllocateVplCB`, `TryAllocateVpl`, `FreeVpl`, `ReferVplStatus`) in `src/hle/kernel/threadman_vpl.cpp`. Re-run created 2 VPLs, opened 2 disc files, spawned the audio polling thread, ran SAS Core (256 frames), **presented 1 frame (`480x272`)**, and exited cleanly at the 600-VBlank limit (`Class: (e)`, `Exit code: 0`). Issue #67 is complete per design §1.

## 3. Static Coverage Delta

* `repsp` registered HLE NIDs: `146` → **`168`** (`+22`)
* `nid_names.cpp` known names: `216` → **`283`** (`+67`)
* Consumer static coverage: **`141 / 229` (88 missing)** → **`158 / 229` (71 missing)**

## 4. Verification

* Extended `tools/sysmem_probe/main.cpp`, `tools/utility_probe/main.cpp`, and `tools/kernel_sched_probe/main.cpp` (VPL allocation, exhaustion, coalescing free, wait/wake handoff, delete); updated `ARCHITECTURE.md` (`repsp_hle_kernel` 80, `repsp_hle_utility` 15).
* **Win64 Debug & Release standalone**: 0 warnings on all `repsp` targets (only the 2 pre-existing third-party `imgui.vcxproj` `C4819` warnings under CP949), **23/23 CTest passed** in both configurations.
