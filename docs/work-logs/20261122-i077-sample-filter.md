# 작업 로그: 아날로그 값까지 바꾸는 컨트롤러 샘플 필터 (issue #77)

* 이슈: [#77](https://github.com/reexec/repsp/issues/77) (소비자 요구: [reexec/reDMP2#19](https://github.com/reexec/reDMP2/issues/19))
* 설계: [20261122-i077-sample-filter.md](../design/20261122-i077-sample-filter.md)
* 브랜치: `i77-sample-filter`

---

## 1. 변경 요약

* `include/repsp/hle/ctrl.h`:
  * `using SampleFilterFn = std::function<void(SceCtrlData& sample)>;` 및 `void SetSampleFilter(SampleFilterFn fn);` (`nullptr`로 해제) 선언을 추가했다.
  * 기존 `SetButtonFilter`는 변경 없이 유지했다.
* `src/hle/ctrl/hle_ctrl.cpp`:
  * `g_sample_filter`와 `SetSampleFilter(SampleFilterFn fn)`을 추가하고 `Register()`와 `Shutdown()`에서 `g_button_filter`와 함께 초기화하도록 했다.
  * 현재 `sceCtrl`이 등록하는 읽기 함수는 `sceCtrlReadBufferPositive` (`HleReadBufferPositive`) 1개뿐이며(`Peek*`, `ReadBufferNegative` 미등록), `HleReadBufferPositive` 안에서 게스트 버퍼 `WriteBlock` 직전 샘플마다 `button_filter` → `sample_filter` 순서로 적용하도록 했다.
  * 입력 녹화(`--input-record`)는 `ProduceSample`에서 원시 값을 기록하므로 필터 영향을 받지 않는다.
* `tools/ctrl_probe/main.cpp`:
  * Check 9를 추가해 (1) 샘플 필터 단독으로 `buttons`/`lx`/`ly` 변경, (2) 버튼 필터와 함께 사용 시 버튼 필터 → 샘플 필터 순서 적용, (3) `nullptr` 해제 시 원시 값 복원, (4) 녹화 파일에는 필터 전 값만 기록됨을 검증했다 (`9/9 checks passed`).
* `ARCHITECTURE.md`:
  * §2.10(한국어·영어)에 `SetSampleFilter(fn)` 계약을 반영했다.

---

## 2. 빌드 및 CTest 검증 (**확인됨**)

* **Win64 Debug**: 프로젝트 소스 컴파일러 경고 **0건**, CTest **23/23 통과** (`ctrl_probe` 9/9 포함).
* **Win64 Release**: 프로젝트 소스 컴파일러 경고 **0건**, CTest **23/23 통과** (`ctrl_probe` 9/9 포함).
* **Linux x64**: CI에서 검증.

---

# Work Log: Controller Sample Filter for Analog Values (issue #77)

* Issue: [#77](https://github.com/reexec/repsp/issues/77) (consumer need: [reexec/reDMP2#19](https://github.com/reexec/reDMP2/issues/19))
* Design: [20261122-i077-sample-filter.md](../design/20261122-i077-sample-filter.md)
* Branch: `i77-sample-filter`

---

## 1. Summary of Changes

* `include/repsp/hle/ctrl.h`:
  * Added `using SampleFilterFn = std::function<void(SceCtrlData& sample)>;` and `void SetSampleFilter(SampleFilterFn fn);` (`nullptr` to clear).
  * Left `SetButtonFilter` unchanged for existing consumers.
* `src/hle/ctrl/hle_ctrl.cpp`:
  * Added `g_sample_filter` and `SetSampleFilter`, cleared alongside `g_button_filter` in both `Register()` and `Shutdown()`.
  * `sceCtrlReadBufferPositive` (`HleReadBufferPositive`) is currently the only registered controller read function (`Peek*` and `ReadBufferNegative` are not registered); per sample before `WriteBlock`, `button_filter` runs first and `sample_filter` runs immediately after on the guest thread.
  * Input recording (`--input-record`) continues to log pre-filter values in `ProduceSample`.
* `tools/ctrl_probe/main.cpp`:
  * Added Check 9 verifying (1) sample filter modifying `buttons`/`lx`/`ly`, (2) combined `button_filter` → `sample_filter` execution order, (3) restoration when cleared, and (4) pre-filter-only input recording (`9/9 checks passed`).
* `ARCHITECTURE.md`:
  * Updated §2.10 (Korean and English) with the `SetSampleFilter(fn)` contract.

---

## 2. Build and CTest Verification (**confirmed**)

* **Win64 Debug**: **0** compiler warnings in project sources, CTest **23/23 passed** (including `ctrl_probe` 9/9).
* **Win64 Release**: **0** compiler warnings in project sources, CTest **23/23 passed** (including `ctrl_probe` 9/9).
* **Linux x64**: Verified in CI.
