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
| 0 (시작점) | `(b) Unimplemented kernel NID`: `SysMemUserForUser` `0x7591C7DB` (`sceKernelSetCompiledSdkVersion`) | `src/hle/kernel/sysmem.cpp`에 `sceKernelSetCompiledSdkVersion` (`0x7591C7DB`), 같은 CRT 시작 묶음의 `sceKernelSetCompilerVersion` (`0xF77D77CB`), 그리고 `UserPartition`에 이미 구현된 조회 메서드를 노출하는 `sceKernelMaxFreeMemSize` (`0xA291F107`), `sceKernelTotalFreeMemSize` (`0xF919F628`) 추가(출처: uOFW / psprecomp `src/hle/sysmem.c`, **추정** → 실행·probe로 **확인됨**). | `sceKernelSetCompiledSdkVersion` 1회, `sceKernelSetCompilerVersion` 1회 호출 후 통과. 게스트가 `user_main` 스레드(우선순위 32, 스택 256 KiB)를 생성·시작한 뒤 해당 스레드 진입점에서 **디스패치 미스**로 정지. |

### 최종 정지 지점 (설계 §1 완료 조건 도달)
* **정지 분류**: `(b) Unexpected runtime/HLE stop` — **미구현 NID가 아닌 디스패치 미스 (`Dispatch miss`, exit code 11)**
* **관측 사실**: `module_start` 스레드가 두 버전 보고 호출을 마치고 `user_main` 스레드를 생성·기동했으나, 소비자 측 재컴파일 코드의 간접 호출 디스패치 표에 `user_main` 진입점이 등록되어 있지 않아 정지함(디스패치 `0`회, 표시 `0`프레임).
* 설계 §1 멈춤 기준("미구현 NID가 아닌 이유로 멈추면 이 issue를 끝내고 그 지점을 보고한다")에 따라 여기서 반복을 종료합니다. 네트워크 모듈(`repsp_hle_net`)과 작은 함수들은 아직 호출되지 않아 미루었습니다.

---

## 3. 정적 임포트 대조 변화 (소비자 측)

* `repsp` 등록 HLE NID 수: `146` → **`150`** (`+4`)
* `nid_names.cpp` 공개 이름 수: `216` → **`283`** (`+67`)
* 소비자 정적 커버리지: **`141 / 229` (미구현 88)** → **`145 / 229` (미구현 84)** (`SysMemUserForUser`: `3/8` → `7/8`)

---

## 4. probe 및 단독 빌드 검증

* `tools/sysmem_probe/main.cpp`: `sceKernelTotalFreeMemSize`, `sceKernelMaxFreeMemSize`(할당으로 인한 분할 시 `max < total` 및 해제 후 복원 확인), `sceKernelSetCompiledSdkVersion`, `sceKernelSetCompilerVersion` 검증을 추가했습니다.
* `ARCHITECTURE.md`: `repsp_hle_kernel` 등록 함수 수(`65` → `69`)를 갱신했습니다.
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

* **Iteration 1**: Stopped on `SysMemUserForUser` `0x7591C7DB` (`sceKernelSetCompiledSdkVersion`). Implemented `sceKernelSetCompiledSdkVersion`, its CRT startup sibling `sceKernelSetCompilerVersion` (`0xF77D77CB`), and the existing `UserPartition` queries `sceKernelMaxFreeMemSize` (`0xA291F107`) and `sceKernelTotalFreeMemSize` (`0xF919F628`) in `src/hle/kernel/sysmem.cpp` (sources: uOFW / psprecomp `src/hle/sysmem.c`, **inferred** → **confirmed** by probe and run).
* **Re-run result**: Both `sceKernelSetCompiledSdkVersion` (1 call) and `sceKernelSetCompilerVersion` (1 call) succeeded; `module_start` created and started `user_main` (prio 32, 256 KiB stack), which immediately stopped with **Dispatch miss** (not an unimplemented NID, exit code 11, 0 dispatches, 0 frames presented).
* Per design §1's stopping rule, the loop ends here and reports the non-NID stop. `repsp_hle_net` and the small utility modules were not reached and remain deferred.

## 3. Static Coverage Delta

* `repsp` registered HLE NIDs: `146` → **`150`** (`+4`)
* `nid_names.cpp` known names: `216` → **`283`** (`+67`)
* Consumer static coverage: **`141 / 229` (88 missing)** → **`145 / 229` (84 missing)** (`SysMemUserForUser`: `3/8` → `7/8`)

## 4. Verification

* Extended `tools/sysmem_probe/main.cpp` to verify `sceKernelTotalFreeMemSize`, `sceKernelMaxFreeMemSize` (including split-heap and post-free restoration), `sceKernelSetCompiledSdkVersion`, and `sceKernelSetCompilerVersion`.
* Updated `ARCHITECTURE.md` (`repsp_hle_kernel` count `65` → `69`).
* **Win64 Debug & Release standalone**: 0 warnings on all `repsp` targets (only the 2 pre-existing third-party `imgui.vcxproj` `C4819` warnings under CP949), **23/23 CTest passed** in both configurations.
