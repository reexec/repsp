# 설계: 아날로그 값까지 바꾸는 컨트롤러 샘플 필터 (issue #77)

소비자 요구: [reexec/reDMP2#19](https://github.com/reexec/reDMP2/issues/19).

## 1. 배경

* v0.0.29(#63)의 `SetButtonFilter(std::function<uint32_t(uint32_t)>)`는 `sceCtrlReadBufferPositive`가 게임에 돌려줄 버튼 마스크만 바꾼다.
* 소비자의 자동 플레이가 아날로그 스틱 레인의 노트를 치려면 `Lx`/`Ly`도 바꿔야 한다.

## 2. 변경

* `include/repsp/hle/ctrl.h`:
  ```cpp
  using SampleFilterFn = std::function<void(SceCtrlData& sample)>;
  void SetSampleFilter(SampleFilterFn fn);   // nullptr로 해제
  ```
* 부르는 자리는 버튼 필터와 같다. 게스트 스레드에서 HLE 호출 안, 샘플마다 버튼 필터 다음에 부른다. 필터는 버튼도 바꿀 수 있다.
* 지금 버튼 필터를 적용하는 읽기 함수에 모두 적용한다. 같은 묶음에서 필터가 빠진 읽기 함수(`Peek*`, `ReadBufferNegative` 등)가 있으면 둘 다 함께 맞춘다. 무엇을 바꿨는지 작업 로그에 적는다.
* 입력 녹화(`--input-record`)는 필터 전 값을 적는다(지금과 같음).
* `Register()`·`Shutdown()`에서 버튼 필터와 함께 초기화한다.
* `SetButtonFilter`는 그대로 둔다. 기존 소비자는 바꿀 필요가 없다.
* 게임 정보는 넣지 않는다.

## 3. 검증

1. `ctrl_probe`에 검사를 더한다:
   * 샘플 필터가 `lx`/`ly`/`buttons`를 바꾸면 게임 버퍼에 그 값이 쓰인다.
   * 버튼 필터와 함께 쓰면 버튼 필터 → 샘플 필터 순서로 적용된다.
   * 해제하면 원래 값이 돌아온다.
   * 녹화에는 필터 전 값이 적힌다.
2. `ARCHITECTURE.md`의 컨트롤러 절에 계약을 적는다.
3. CTest 전부 통과. Win64 Debug/Release 경고 0. Linux는 CI.

---

# Design: a controller sample filter that can change analog values (issue #77)

Consumer need: [reexec/reDMP2#19](https://github.com/reexec/reDMP2/issues/19).

## 1. Background

* v0.0.29 (#63) added `SetButtonFilter(std::function<uint32_t(uint32_t)>)`. It changes only the button mask that `sceCtrlReadBufferPositive` returns to the game.
* For the consumer's autoplay to hit notes on the analog-stick lane, `Lx`/`Ly` must change too.

## 2. Changes

* `include/repsp/hle/ctrl.h` gains `SampleFilterFn` and `SetSampleFilter` (listing above; `nullptr` clears it).
* It is called where the button filter is: on the guest thread inside the HLE call, per sample, after the button filter. The filter may change buttons too.
* It applies to every read function the button filter applies to today. If a read function in the same family lacks the filter (`Peek*`, `ReadBufferNegative`, ...), both filters are added to it together; the work log records what changed.
* Input recording (`--input-record`) logs pre-filter values, as now.
* `Register()` and `Shutdown()` clear it along with the button filter.
* `SetButtonFilter` stays as is, so existing consumers need no change.
* No game information goes in.

## 3. Verification

1. `ctrl_probe` gains checks:
   * when the sample filter changes `lx`/`ly`/`buttons`, those values reach the guest buffer
   * with both filters set, the order is button filter, then sample filter
   * clearing restores the original values
   * recordings hold pre-filter values
2. The controller section of `ARCHITECTURE.md` documents the contract.
3. All CTest pass; Win64 Debug/Release with zero warnings; Linux in CI.
