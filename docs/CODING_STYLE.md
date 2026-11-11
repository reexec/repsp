# 코딩 스타일

이 규칙은 [rexbox의 CODING_STYLE](https://github.com/reexec/rexbox/blob/main/docs/CODING_STYLE.md)과 같다(원래 [rePIU의 CODING_STYLE](https://github.com/reexec/rePIU/blob/main/docs/CODING_STYLE.md)에서 왔다). 소비자 프로젝트(예: [reDMP1](https://github.com/reexec/reDMP1/blob/main/docs/CODING_STYLE.md))도 같은 규칙을 쓴다.

These rules are those of [rexbox's CODING_STYLE](https://github.com/reexec/rexbox/blob/main/docs/CODING_STYLE.md) (originally from [rePIU's CODING_STYLE](https://github.com/reexec/rePIU/blob/main/docs/CODING_STYLE.md)). Consumer projects (for example [reDMP1](https://github.com/reexec/reDMP1/blob/main/docs/CODING_STYLE.md)) use the same rules.

## 기본 원칙

* 런타임, 도구, probe의 C++ 코드는 C++20을 기준으로 작성한다. C 코드는 C11을 기준으로 한다(C에서 부를 수 있는 공개 계약은 C11 헤더로 둔다).
* 기본 기준은 Google C++ Style Guide를 따른다. 프로젝트 예외 규칙은 아래 항목으로 명시한다.
* 원본 실행 파일의 동작 보존을 우선하며, 편의를 위한 게임 로직 재작성은 피한다.
* 플랫폼 공용 코어와 플랫폼 종속 구현을 분리한다(`src/platform/<os>/`).
* 큰 구조 변경은 설계 문서에 근거를 남긴다.
* 새 기능에는 최소 검증 절차를 함께 문서화한다.
* 독립된 책임을 갖는 큰 기능은 전용 header/source pair로 분리한다.
* 공개 계약(`include/repsp/`)에는 구현 세부를 노출하지 않는다.

## Basic Principles

* Write the runtime, tool and probe C++ code against C++20, and C code against C11 (public contracts callable from C are C11 headers).
* Follow the Google C++ Style Guide as the baseline. Project-specific exceptions are defined below.
* Prioritize preserving original executable behavior and avoid rewriting game logic for convenience.
* Separate the platform-neutral core from platform-specific implementations (`src/platform/<os>/`).
* Document the rationale for large structural changes in design documents.
* Document a minimum verification procedure with each new feature.
* Give major independently responsible features dedicated header/source pairs.
* Public contracts (`include/repsp/`) expose no implementation details.

## 프로젝트 예외 규칙

* 여는 중괄호 `{` 는 같은 줄 끝에 두지 않고 다음 줄에 둔다. 한 줄 접근자(`int size() const { return size_; }`)는 예외로 허용한다.
* 탭 문자는 사용하지 않는다.
* 들여쓰기는 공백 4칸을 사용한다.
* 함수명은 `PascalCase`, 변수명은 `snake_case`, 클래스 멤버 변수는 `snake_case_`, 상수는 `kPascalCase`를 사용한다.
* C++ 소스 파일 확장자는 `.cpp`, 헤더는 `.h`를 사용한다. C 소스는 `.c`를 사용한다.
* 네임스페이스: `repsp::<layer>`(예: `repsp::hle::ge`, `repsp::runtime`).
* include 경로는 `include/` 기준(`"repsp/runtime/guest_memory.h"`)으로 쓴다.
* include guard는 경로를 대문자로 바꾼 `REPSP_RUNTIME_GUEST_MEMORY_H` 형식을 쓴다.

## Project-Specific Exceptions

* Place opening braces `{` on the next line instead of at the end of the current line. One-line accessors (`int size() const { return size_; }`) are allowed as an exception.
* Do not use tab characters.
* Use four spaces for indentation.
* Use `PascalCase` for functions, `snake_case` for variables, `snake_case_` for class members, and `kPascalCase` for constants.
* Use `.cpp` for C++ sources and `.h` for headers. C sources use `.c`.
* Namespaces: `repsp::<layer>` (for example `repsp::hle::ge`, `repsp::runtime`).
* Write include paths relative to `include/` (`"repsp/runtime/guest_memory.h"`).
* Include guards spell the path in upper case, as in `REPSP_RUNTIME_GUEST_MEMORY_H`.

## 재컴파일 코드 계약

* repsp는 생성 코드도, lifter의 헤더도 갖지 않는다. 재컴파일 코드와의 접점은 `include/repsp/recomp/`의 lifter 중립 계약(C 호환 함수와 콜백 표)뿐이다.
* 계약에는 lifter 고유의 이름(`psp_cpu`, `psp_func_*` 등)이나 레지스터 파일 레이아웃을 넣지 않는다. 레지스터 파일은 소비자의 어댑터가 소유하고, repsp는 어댑터가 등록한 콜백(컨텍스트 저장·복원, 인자·반환 접근, 게스트 함수 호출)으로만 다룬다.
* 계약을 바꾸면 `ARCHITECTURE.md`의 소비자 계약 절과 계약 probe를 같은 작업에서 갱신한다.

## Recompiled-Code Contract

* repsp holds neither generated code nor any lifter's headers. Its only meeting point with recompiled code is the lifter-neutral contract in `include/repsp/recomp/` (C-compatible functions and a callback table).
* The contract carries no lifter-specific names (`psp_cpu`, `psp_func_*`, ...) and no register-file layout. The consumer's adapter owns the register file; repsp handles it only through the callbacks the adapter registers (saving and restoring the context, argument and return access, calling guest functions).
* A contract change updates the consumer-contract section of `ARCHITECTURE.md` and the contract probe in the same task.

## 주석 언어

* **소스 코드의 주석은 영어로만 작성한다.** 한국어 주석과 한국어·영어 이중 언어 주석을 두지 않는다. 같은 내용을 두 벌 유지하면 한쪽만 갱신되어 어긋난다.
* 이 규칙은 문서 규칙과 다르다. `docs/` 아래 Markdown 문서는 한국어를 먼저 쓰고 영어 번역을 덧붙이지만, 소스 주석은 영어 한 벌만 둔다.
* 커밋 메시지, 코드 식별자, 로그 문자열도 영어를 사용한다.
* 적용 대상은 C/C++ 소스·헤더와 빌드·스크립트(`scripts/`, `CMakeLists.txt`, 워크플로 파일)를 포함한 저장소의 모든 코드다. `third_party/` 아래 외부 코드는 원본 그대로 둔다.

## Comment Language

* **Write source-code comments in English only.** Do not keep Korean or bilingual comments; two copies of one explanation drift apart.
* This differs from the documentation rule: Markdown under `docs/` leads with Korean and adds English, while source comments carry one English copy only.
* Commit messages, code identifiers and log strings are English as well.
* The scope is all code in the repository, including C/C++ sources and headers and the build and script files (`scripts/`, `CMakeLists.txt`, workflow files). External code under `third_party/` stays as upstream wrote it.

## 예시

잘못된 예:

```cpp
int main() {
    if (ready) {
        Run();
    }
}
```

올바른 예:

```cpp
int main()
{
    if (ready)
    {
        Run();
    }
}
```

## Example

The incorrect and correct forms are the two blocks above: braces move to their own lines.

## 디렉터리 정책

* 공개 계약은 `include/repsp/<layer>/`, 구현은 `src/<layer>/`에 둔다.
* 플랫폼 구현은 `src/platform/<os>/`, 공유 백엔드는 `src/platform/sdl/`, `src/gpu/gl/`에 둔다.
* HLE 모듈은 펌웨어 모듈 단위로 `src/hle/<module>/`에 둔다(예: `src/hle/threadman/`).
* 게임 없이 실행하는 검사는 `tools/<name>_probe/`에 두고 CTest에 등록한다.
* 공개 헤더에서 플랫폼을 고르는 `#if`는 두지 않는다.

## Directory Policy

* Public contracts go in `include/repsp/<layer>/`, implementations in `src/<layer>/`.
* Platform implementations go in `src/platform/<os>/`, shared backends in `src/platform/sdl/` and `src/gpu/gl/`.
* HLE modules go in `src/hle/<module>/`, one per firmware module (for example `src/hle/threadman/`).
* Checks that run without a game go in `tools/<name>_probe/` and are registered in CTest.
* No `#if` chooses a platform in a public header.

## 라이선스 정책

* 프로젝트 라이선스는 `MIT License`다.
* GPL, LGPL, AGPL 등 전염성 라이선스의 서드파티 코드는 빌드에 넣지 않는다.
* 서드파티 의존성을 추가하기 전에 라이선스를 확인하고 `THIRD_PARTY_NOTICES.md`에 기록한다.

## License Policy

* The project license is the `MIT License`.
* Do not build third-party code under copyleft licenses such as GPL, LGPL, or AGPL.
* Check third-party dependency licenses before adding them and record them in `THIRD_PARTY_NOTICES.md`.
