# AGENTS.md

이 문서의 작업 규칙과 코딩 규칙은 [reexec/rexbox](https://github.com/reexec/rexbox)의 규칙과 같으며(그 규칙은 [nworkers/reJSRF](https://github.com/nworkers/reJSRF)와 [reexec/rePIU](https://github.com/reexec/rePIU)에서 왔다), 이 프로젝트의 성격(여러 타이틀이 공유하는 PSP 런타임 라이브러리)과 라이선스(MIT)에 맞게 고쳤다. 소비자 프로젝트(예: [reexec/reDMP1](https://github.com/reexec/reDMP1))와 같은 규칙으로 일한다.

The working and coding rules here are those of [reexec/rexbox](https://github.com/reexec/rexbox) (themselves from [nworkers/reJSRF](https://github.com/nworkers/reJSRF) and [reexec/rePIU](https://github.com/reexec/rePIU)), adapted to this project's nature (a PSP runtime library shared by several titles) and license (MIT). The consumer projects (for example [reexec/reDMP1](https://github.com/reexec/reDMP1)) work under the same rules.

## 프로젝트 개요

repsp는 원본 PSP 타이틀을 정적 재컴파일로 네이티브 실행하는 프로젝트들이 공유하는 **공용 PSP 런타임**이다. 특정 타이틀에 종속되지 않는다. 디스크·모듈 컨테이너(ISO9660, PBP, `~PSP`, ELF/PRX) 로더, 게스트 메모리, NID 레지스트리와 모듈 import 연결, 게스트 스레드 스케줄러, lifter에 중립적인 재컴파일 코드 계약(게스트 CPU 컨텍스트, 게스트 함수 호출, NID 호출 진입점), PSP 펌웨어 라이브러리의 HLE 모듈, GE 디스플레이 리스트 해석기와 렌더러, 호스트 플랫폼 계층(가상 메모리, 스레드, SDL3 + OpenGL 표시·입력·오디오)을 담는다.

타이틀 프로젝트는 이 저장소를 submodule로 가져가 타이틀 계층(프로파일, 디컴파일 코드, 재컴파일 설정)과 앱만 더한다. 원본 Allegrex 코드를 C로 옮기는 재컴파일(lifter 실행)과 디컴파일은 타이틀 프로젝트의 일이며, repsp는 그 코드가 부르는 PSP를 제공한다.

## Project Overview

repsp is the **title-agnostic PSP runtime** shared by projects that run original PSP titles natively through static recompilation. It holds the disc and module container loaders (ISO9660, PBP, `~PSP`, ELF/PRX), guest memory, the NID registry and module import binding, the guest thread scheduler, a lifter-neutral contract for recompiled code (the guest CPU context, calling guest functions, the NID call entry), the HLE modules for the PSP firmware libraries, the GE display-list interpreter and renderer, and the host platform layer (virtual memory, threads, display, input and audio on SDL3 + OpenGL).

A title project takes this repository as a submodule and adds only its title layer (profile, decompiled code, recompilation configuration) and its application. Recompiling the original Allegrex code to C (running the lifter) and decompiling are the title project's work; repsp provides the PSP that code calls.

---

## 핵심 설계 목표

우선순위:

1. repsp는 특정 타이틀에 의존하지 않는다. 타이틀 이름, 타이틀 주소, 타이틀 헤더가 이 저장소에 나타나면 규칙 위반이다. 타이틀이 알려줘야 하는 것(앱 이름, 표시 기본값, 타이틀별 우회)은 공개 계약(`include/repsp/`)으로 받는다.
2. 원본 게임 로직을 보존한다. 재컴파일된 코드가 기준 구현이며 HLE는 그 코드가 부르는 API의 동작을 원본 펌웨어와 같게 한다.
3. HLE는 API 경계(펌웨어 라이브러리의 NID export, 시스템 콜)에서 한다. 하드웨어 수준 처리는 API 경계로 해결되지 않는 곳에만 둔다(GE 디스플레이 리스트 해석이 대표적이다).
4. Win64를 먼저 완성하고, 이후 Linux x64, Win32, Linux x86, Web, Android로 넓힌다. 처음부터 멀티플랫폼 공용 구조로 설계한다.
5. 에뮬레이터 소스(PPSSPP 등)를 통합하지 않는다.
6. 게임 데이터(ISO, 실행 파일, PRX, 자산)와 그것에서 기계적으로 생성한 코드는 저장소에 넣지 않는다. 이 저장소에는 어떤 타이틀의 데이터도 없다.

## Primary Design Goals

Priority order:

1. repsp knows no title. A title's name, addresses or headers appearing in this repository is a rule violation. What a title must tell it (the application name, display defaults, title-specific workarounds) comes through the public contracts in `include/repsp/`.
2. Preserve the original game logic. The recompiled code is the reference implementation, and HLE makes the APIs that code calls behave as the original firmware does.
3. Do HLE at API boundaries (firmware library NID exports, syscalls). Hardware-level processing exists only where an API boundary cannot work (interpreting GE display lists is the main case).
4. Finish Win64 first, then widen to Linux x64, Win32, Linux x86, the web and Android. Design shared multiplatform structures from the start.
5. Do not integrate emulator source code (PPSSPP and others).
6. Never put game data (ISO, executables, PRX modules, assets) or code mechanically generated from it in the repository. This repository holds no title's data at all.

---

## 참조 문서

아키텍처 변경 전에는 관련 문서를 먼저 읽고 갱신한다.

* `docs/PROJECT_CHARTER.md`
* `ARCHITECTURE.md`
* `docs/CODING_STYLE.md`
* `THIRD_PARTY_NOTICES.md`
* `docs/design/`
* `docs/work-orders/`
* `docs/work-logs/`
* `docs/kb/`
* `docs/guides/`

타이틀에서 확인한 바이너리 분석(`docs/analysis/`, `docs/EXECUTABLE_DESIGN.*`)은 타이틀 프로젝트에 둔다. repsp의 동작을 바꾼 근거가 어느 타이틀의 분석이면 그 문서를 링크한다.

## Reference Documents

Read and update the relevant documents before making architectural changes: the list above. Binary analysis confirmed on a title (`docs/analysis/`, `docs/EXECUTABLE_DESIGN.*`) stays in the title project; when a title's analysis is the ground for changing repsp's behavior, link that document.

---

## 핵심 원칙

1. 사용자 요구사항을 받으면 바로 구현하지 말고 먼저 설계를 작성하거나 기존 설계를 갱신한다.
2. 설계가 정리되면 구현 계획서를 작성하거나 갱신한 뒤 구현을 시작한다.
3. 코드 변경이 있는 모든 작업은 작업 지시 문서와 작업 로그 문서를 남긴다.
4. 문서는 기본적으로 한국어를 먼저 쓰고 바로 아래에 영어 번역을 추가한다. 이 규칙은 `docs/` 아래 문서와 `README.md`, `ARCHITECTURE.md` 같은 저장소 문서에만 적용되며, 소스 코드 주석에는 적용하지 않는다.
5. 기존 코드, 자산, 문서의 의도를 확인한 뒤 수정하며, 큰 구조 변경은 근거를 문서에 남긴다.
6. 멀티플랫폼 공용 구조를 우선 설계하고, 플랫폼별 세부 사항은 분리한다.

## Core Principles

1. When a user requirement is received, do not implement immediately. First write a design or update the existing design.
2. After the design is clear, write or update the implementation work order before starting implementation.
3. Every task that changes code must leave a work-order document and a work-log document.
4. Documents should be written in Korean first, followed immediately by an English translation. This applies to documents under `docs/` and repository documents such as `README.md` and `ARCHITECTURE.md`; it does not apply to source-code comments.
5. Confirm the intent of existing code, assets, and documents before modifying them. Document the rationale for large structural changes.
6. Design shared multiplatform structures first, and keep platform-specific details separated.

---

## 응답 태도 규칙

* 모든 질문에 대한 답변은 존대말을 사용하고 예의 있는 태도를 유지한다.

## Response Tone Rules

* Use polite Korean speech and maintain a respectful attitude for all answers.

---

## 요구사항 처리 절차

1. 요구사항 접수
2. 관련 코드, 문서, 자산 맥락 확인
3. 설계 문서 작성 또는 갱신
4. 구현 계획서 작성 또는 갱신
5. 사용자 승인 또는 현재 진행 맥락 확인
6. 구현
7. 빌드 또는 범위에 맞는 검증
8. 작업 로그 작성

단, 요구사항이 단순 질문이거나 확인 요청이라면 해당 내용에 바로 응답하고 문서화 및 코드 구현은 하지 않는다.

## Requirement Handling Procedure

1. Receive the requirement.
2. Inspect the relevant code, documentation, and asset context.
3. Write or update the design document.
4. Write or update the implementation work order.
5. Confirm user approval or the current active context.
6. Implement.
7. Run a build or verification appropriate to the scope.
8. Write the work log.

If the requirement is a simple question or confirmation request, answer it directly without documentation or code implementation.

---

## 문서 작성 규칙

* 설계 문서는 `docs/design/`, 계획 문서는 `docs/work-orders/`, 작업 결과와 회고는 `docs/work-logs/` 아래에 둔다.
* 사용자가 직접 수행하는 검증·측정·운영 절차는 `docs/guides/` 아래에 둔다. 반복 수행 가능한 절차만 두고, 근거가 되는 작업 로그와 설계를 링크한다.
* PSP 일반 지식(컨테이너 형식, Allegrex·VFPU, 펌웨어 모듈과 NID, GE 명령, 오디오 형식)은 `docs/kb/` 아래에 주제별로 둔다. 소비자 프로젝트는 일반 PSP 지식을 여기에 더한다. 외부 자료에서 왔으면 원문 링크를 단다.
* 프로젝트의 큰 방향성은 `docs/PROJECT_CHARTER.md`에, 현재 구현되는 코드의 설계와 구조와 소비자 계약은 `ARCHITECTURE.md`에 지속적으로 반영한다.
* 규칙이 바뀌면 `AGENTS.md`와 관련 문서를 함께 갱신한다.
* 작업 단위 문서 파일명은 `YYYYMMDD-i###-slug.md` 형식을 사용한다. `###`은 이 저장소의 GitHub issue 번호를 세 자리로 맞춘 값이다.
* 구조, 관계, 흐름, 호출 순서, 상태 전이, 주소 변환은 Mermaid로 도식화한다. 세 개 이상의 구성요소·단계·분기·계층이 있으면 적용을 검토하고, 단순 사실에는 강제하지 않는다. 도식은 본문과 같은 사실과 확인 상태를 표현하며 본문을 대체하지 않는다.
* `docs/kb/`는 **확인됨**, **추정**, **미확정**을 구분하고, 다른 프로젝트(psprecomp, PPSSPP, uOFW 등)에서 얻은 주장은 실제 타이틀이나 테스트로 다시 확인하기 전까지 **추정**으로 적고 출처를 단다. kb 파일을 추가하거나 이름을 바꾸면 `docs/kb/README.md` 색인을 같은 작업에서 갱신한다.

## Documentation Rules

* Design documents go under `docs/design/`, work orders under `docs/work-orders/`, results and retrospectives under `docs/work-logs/`.
* Procedures the user runs themselves (verification, measurement, operations) go under `docs/guides/`: repeatable procedures only, linking the design and work log they rest on.
* General PSP knowledge (container formats, the Allegrex and VFPU, firmware modules and NIDs, GE commands, audio formats) goes under `docs/kb/` by topic; consumer projects extend general PSP knowledge here. Link external sources when derived from them.
* Reflect the broad direction in `docs/PROJECT_CHARTER.md`, and the design, structure and consumer contract of the implemented code in `ARCHITECTURE.md`, continuously.
* When rules change, update `AGENTS.md` and the related documents together.
* Task documents are named `YYYYMMDD-i###-slug.md`, with `###` this repository's GitHub issue number padded to three digits.
* Use Mermaid for structure, relationships, flows, call sequences, state transitions and address translation; consider it whenever there are three or more components, steps, branches or layers, and do not force it on single facts. Diagrams express the same facts and confirmation status as the prose and do not replace it.
* `docs/kb/` distinguishes **confirmed**, **inferred** and **unresolved**; claims from other projects (psprecomp, PPSSPP, uOFW, ...) stay **inferred**, with their source, until re-checked on a real title or a test. Adding or renaming a kb file updates the `docs/kb/README.md` index in the same task.

---

## 레이어와 의존 규칙

* 레이어는 아래에서 위로 `platform` → `module`(컨테이너·ELF/PRX) → `runtime` → `recomp`, `hle/<module>`, `gpu` 순서다. 각 레이어는 아래 레이어만 링크한다. CMake 타깃 그래프가 이 규칙을 강제한다.
* HLE 모듈(`repsp_hle_<module>`)은 런타임(필요하면 `gpu`, 오디오 계약)만 링크하고 다른 HLE 모듈을 링크하지 않는다. 모듈 사이의 협력은 런타임의 계약(예: 스케줄러, 커널 객체 표, 이벤트)을 거친다.
* 플랫폼 계층은 런타임과 타이틀의 타입을 알지 못한다. 기본 값(주소, 크기, 버퍼, 콜백)으로만 호출된다.
* 공개 계약은 `include/repsp/`에 있고, 소비자는 그것만 include한다. 내부 헤더는 `src/` 아래에 둔다.
* repsp는 어떤 lifter에도 의존하지 않는다. lifter의 헤더, 소스, 생성 코드를 include·링크·submodule로 갖지 않는다. lifter의 ABI(예: psprecomp의 `psp_*` 선언)를 repsp의 재컴파일 코드 계약에 잇는 어댑터는 소비자 프로젝트에 둔다.

## Layer and Dependency Rules

* Layers run bottom to top: `platform` → `module` (containers, ELF/PRX) → `runtime` → `recomp`, `hle/<module>`, `gpu`. Each layer links only the layers below it; the CMake target graph enforces this.
* An HLE module (`repsp_hle_<module>`) links the runtime (and, where needed, `gpu` or the audio contract) only, never another HLE module. Modules cooperate through runtime contracts (the scheduler, the kernel object table, events).
* The platform layer knows none of the runtime's or the title's types. It is called with plain values (addresses, sizes, buffers, callbacks).
* Public contracts live in `include/repsp/`, and consumers include only those; internal headers stay under `src/`.
* repsp depends on no lifter: it neither includes, links nor carries as a submodule any lifter's headers, sources or generated code. The adapter that connects a lifter's ABI (for example psprecomp's `psp_*` declarations) to repsp's recompiled-code contract lives in the consumer project.

---

## 구현 규칙

* 플랫폼(OS) 종속 코드는 `src/platform/<os>/` 아래에 둔다. 여러 OS가 공유하는 백엔드(SDL3, OpenGL)는 `src/platform/sdl/`, `src/gpu/gl/`처럼 백엔드 이름 디렉터리에 둔다. 그 밖의 디렉터리에는 플랫폼·아키텍처 하위 디렉터리를 두지 않는다.
* 게스트 주소는 항상 32비트 `GuestAddress`이며 호스트 포인터로 바꿀 때는 `GuestMemory::Translate`를 거친다(based 주소 모델). 게스트 주소를 호스트 포인터로 직접 쓰지 않는다. PSP의 캐시 미러(0x40000000 uncached, 0x80000000 kernel)는 Translate가 같은 물리 주소로 접는다.
* 새 기능을 추가할 때는 테스트 전략 또는 최소 검증 절차를 문서에 함께 남긴다. 게임 없이 검사할 수 있는 부분은 probe(CTest)로 만든다.
* 코드 수정이 있는 작업은 영향 범위에 맞는 빌드 검증을 수행하고, 불가능하면 이유를 작업 로그에 남긴다.
* 코딩 스타일 세부 규칙은 `docs/CODING_STYLE.md`에서 관리한다. 소스 코드 주석은 영어로만 작성한다.
* 큰 기능은 하나의 거대 파일에 누적하지 않는다. 독립적으로 이름 붙일 수 있는 하위 시스템은 전용 header/source로 분리하고, 통합 지점에는 orchestration과 adapter 코드만 남긴다.

## Implementation Rules

* Platform (OS) specific code goes under `src/platform/<os>/`. Backends shared by several OSes (SDL3, OpenGL) go in directories named for the backend, such as `src/platform/sdl/` and `src/gpu/gl/`. No other directory has platform or architecture subdirectories.
* A guest address is always a 32-bit `GuestAddress`, turned into a host pointer only through `GuestMemory::Translate` (the based address model); guest addresses are never used directly as host pointers. Translate folds the PSP's cache mirrors (0x40000000 uncached, 0x80000000 kernel) onto the same physical address.
* When adding a feature, document the test strategy or minimum verification procedure. Whatever can be checked without a game becomes a probe (CTest).
* For tasks that modify code, run build verification appropriate to the impact. If verification is impossible, record the reason in the work log.
* Detailed coding style rules live in `docs/CODING_STYLE.md`. Source-code comments are English only.
* Do not accumulate major features in one monolithic file. Give independently named subsystems their own header/source, leaving only orchestration or adapter code at the integration point.

---

## 게임 데이터 규칙

* 어떤 타이틀의 ISO, 실행 파일, PRX, 자산, 그것에서 생성한 코드도 커밋하지 않는다. 테스트 픽스처도 손으로 만든 합성 데이터만 쓴다.
* PSP 복호화 키 자료는 커밋하지 않는다. 키가 필요한 기능은 사용자가 제공한 키 파일을 읽는다.
* Sony 펌웨어 바이너리(PRX, flash 덤프)를 저장소에 넣거나 빌드에 포함하지 않는다.
* 커밋 전에 `git status`로 게임 데이터가 staging되지 않았는지 확인한다.

## Game Data Rules

* No title's ISO, executables, PRX modules, assets, or code generated from them is ever committed. Test fixtures use hand-made synthetic data only.
* PSP decryption key material is never committed; features that need keys read a key file the user supplies.
* Sony firmware binaries (PRX modules, flash dumps) never enter the repository or the build.
* Before committing, check with `git status` that no game data is staged.

---

## 라이선스 규칙

* 프로젝트 라이선스는 `MIT License`다.
* 서드파티 오픈소스는 MIT, BSD, zlib, ISC, Apache-2.0, Boost 같은 허용적 라이선스만 빌드에 포함한다.
* GPL, LGPL, AGPL 같은 전염성 라이선스의 코드는 컴파일·링크하지 않고 저장소에 복사하지도 않는다. 특히 PPSSPP(GPL-2.0+)와 FFmpeg(LGPL/GPL)의 코드는 쓰지 않는다. ATRAC3plus, H.264 같은 코덱은 허용적 라이선스의 구현만 쓰며, 없으면 설계 문서에 대안을 남기고 사용자 확인을 받는다.
* psprecomp(MIT) 같은 허용적 라이선스 프로젝트의 코드는 출처와 커밋을 적고 `THIRD_PARTY_NOTICES.md`를 갱신한 뒤 repsp의 타입과 계약으로 고쳐 옮겨 올 수 있다(옮긴 코드는 원 프로젝트에 의존하지 않는다). 옮긴 코드에서 찾은 버그는 원 프로젝트에도 기여한다.
* 전염성 라이선스 프로젝트(PPSSPP 등)는 동작을 이해하는 지식 출처와 동작 비교 oracle로만 참고하고, 코드를 옮기지 않는다. 공개 문서와 MIT 라이선스의 펌웨어 역공학 자료(uOFW 등)는 라이선스를 확인하고 출처를 달아 참고한다.
* 라이선스가 명시되지 않았거나 LLM 기여를 금지한 프로젝트의 코드는 가져오지 않는다. 공개 문서의 사실은 출처를 달아 참고할 수 있다.
* 서드파티 코드를 가져오면 같은 작업에서 `THIRD_PARTY_NOTICES.md`를 갱신한다. 예외가 필요하면 먼저 문서화하고 사용자 확인을 받는다.

## License Rules

* The project license is the `MIT License`.
* Only permissively licensed third-party code (MIT, BSD, zlib, ISC, Apache-2.0, Boost, ...) is built into the project.
* Copyleft code (GPL, LGPL, AGPL) is neither compiled, linked, nor copied into the repository. In particular no code from PPSSPP (GPL-2.0+) or FFmpeg (LGPL/GPL) is used. Codecs such as ATRAC3plus and H.264 come only from permissively licensed implementations; when none exists, the design document records the alternatives and the user decides.
* Code from permissively licensed projects such as psprecomp (MIT) may be carried over, reshaped to repsp's types and contracts, with its source and commit noted and `THIRD_PARTY_NOTICES.md` updated (carried code does not depend on its origin). Bugs found in carried code are contributed back to the origin too.
* Copyleft projects (PPSSPP, ...) are consulted only as sources of knowledge about behavior and as behavior oracles; their code is not carried over. Public documents and MIT-licensed firmware reverse-engineering material (uOFW, ...) are consulted after checking their license, with attribution.
* Do not take code from projects with no stated license or that forbid LLM contributions. Facts from their public documents may be used with attribution.
* When bringing in third-party code, update `THIRD_PARTY_NOTICES.md` in the same task. If an exception is needed, document it and confirm with the user first.

---

## 작업 단위 규칙

* task는 이 저장소의 GitHub issue로 만든다. task 번호는 GitHub issue 번호를 쓴다.
* 기존 task의 후속 작업은 새 issue를 만들지 않고 그 task의 issue에 붙인다.
* 커밋 제목 끝에 관련 issue 번호를 `(#6)`처럼 붙인다.
* 의미 있는 작업마다 하나의 작업 지시 문서를 만들고, 끝나면 대응되는 작업 로그를 남긴다.
* 설계 없이 바로 코드만 추가하지 않는다. 초기 구조 작업이라도 디렉터리 목적과 향후 확장 방향을 문서로 남긴다.
* 소비자 프로젝트의 필요로 시작한 작업은 그 프로젝트의 issue를 이 저장소의 issue와 작업 로그에 링크한다.

## Task Unit Rules

* A task is an issue in this repository, and the task number is the GitHub issue number.
* Follow-up work on an existing task goes to that task's issue rather than a new one.
* End commit titles with the related issue number, as in `(#6)`.
* Create one work-order document per meaningful task and the corresponding work log when it is done.
* Do not add code directly without a design. Even initial structure work documents the directory purpose and future extension direction.
* Work started for a consumer project's need links that project's issue from this repository's issue and work log.

---

## Git 작업 규칙

* 사용자가 작업을 요청하면 먼저 현재 Git 브랜치명을 확인한다.
* 현재 브랜치가 `main`이면 사용자가 요청한 작업 내용을 바탕으로 작업용 브랜치를 새로 만든 뒤 작업한다.
* 현재 브랜치가 `main`이 아니면 새 브랜치를 만들지 않고 그 브랜치에서 이어서 작업한다. 다른 issue의 작업이어도 같다. 커밋마다 제목 끝에 해당 issue 번호를 붙여 구분한다.
* 작업 단위가 하나 끝날 때마다 관련 변경을 Git 커밋으로 남긴다.
* 프로젝트 버전은 저장소 루트의 `VERSION` 파일에서 `major.minor.patch` 형식으로 관리한다.
* 사용자가 머지를 요청하면 `main`에 머지하기 전에 patch 버전을 1 증가시킨다. minor·major 증가 요청은 각각 하위 번호를 0으로 리셋한다.
* 사용자가 머지를 요청하면 작업 브랜치를 원격에 push하고 GitHub pull request를 만든 뒤(머지 요청이 이 push와 PR 생성의 승인이다), 그 PR을 squash merge해 `main`에 넣는다. 머지한 뒤 `git pull`로 로컬 `main`을 원격과 같게 갱신한다.
* PR 본문에는 해결하는 issue를 `Closes #6`처럼 issue마다 적는다.
* squash merge의 커밋 제목은 작업 브랜치 안의 커밋 제목들을 확인하고, 전체 변경 내용을 잘 표현하는 제목으로 만든다.
* `main`에 머지한 뒤에는 그 머지 커밋에 `VERSION`과 같은 값의 annotated tag를 `vmajor.minor.patch` 형식으로 붙이고, tag 메시지에 핵심 변경을 한 줄로 남긴다. tag는 로컬까지만 만들고 원격 push는 사용자가 직접 수행한다.
* 머지가 완료되면 현재 작업 브랜치를 삭제한다.
* 릴리스 노트(`docs/release-notes/v<version>.md`)에는 해결된 issue 번호와 그 커밋 ID를 함께 적는다.
* submodule(`third_party/SDL` 등)은 upstream의 태그 또는 릴리스 브랜치 커밋에 고정한다.
* `main`에 push되면 Actions(`ci.yml`)가 빌드와 probe(CTest)를 검증한다. 태그를 push하면 Actions(`release.yml`)가 태그가 `VERSION`과 같고 `main` 위에 있는지 확인한 뒤 같은 빌드·테스트를 하고, 릴리스 노트가 있으면 그 내용으로 GitHub Release를 만든다(소스만). 그러므로 릴리스 노트는 태그를 push하기 전에 `main`에 머지되어 있어야 한다.
* 소비자 프로젝트는 머지된 커밋이나 태그에 고정하는 것을 원칙으로 한다. 작업 브랜치 커밋에 고정하는 것은 그 브랜치가 머지되기 전까지의 임시 상태다.

## Git Workflow Rules

* When the user requests work, first check the current Git branch name.
* If the current branch is `main`, create a task branch based on the requested work before making changes.
* If the current branch is not `main`, keep working on it without creating a new branch, even for a different issue. Each commit title ends with its own issue number.
* Leave a Git commit for the related changes whenever one task unit is complete.
* Manage the project version in the repository-root `VERSION` file using `major.minor.patch`.
* When the user requests a merge, increment the patch version by 1 before merging into `main`; minor and major bumps on request reset the lower numbers to 0.
* When the user requests a merge, push the task branch, open a GitHub pull request from it (the merge request approves this push and the pull request), and squash-merge it into `main`. Afterwards update local `main` with `git pull`.
* Name each issue the pull request resolves in its description, as in `Closes #6`.
* For the squash merge's commit title, inspect the branch's commit titles and write one that describes the complete change.
* After merging into `main`, tag that merge commit with an annotated tag matching `VERSION` (`vmajor.minor.patch`) whose message is a one-line summary of the key change. Create tags locally only; the user pushes them.
* Delete the task branch after the merge is complete.
* Release notes (`docs/release-notes/v<version>.md`) record the resolved issue numbers together with their commit IDs.
* Submodules (`third_party/SDL`, ...) are pinned to upstream tags or release-branch commits.
* A push to `main` is verified by Actions (`ci.yml`): the build and the probes (CTest). A pushed tag is verified by Actions (`release.yml`): the tag must equal `VERSION` and sit on `main`, the same build and tests run, and a GitHub Release is created from the release note when it exists (source only). The release note must therefore be merged into `main` before the tag is pushed.
* Consumer projects pin merged commits or tags as a principle; a pin on a task-branch commit is temporary until that branch is merged.

---

## 아키텍처 규칙

* 재컴파일된 원본 코드가 항상 기본 실행 경로다. repsp는 그 코드가 부르는 펌웨어와 하드웨어만 대신한다.
* 운영체제(펌웨어), 시스템 라이브러리, 하드웨어 인터페이스만 HLE로 대체한다. 게임이 디스크에서 올리는 Sony 사용자 모드 PRX도 펌웨어 라이브러리로 보고 NID 경계에서 HLE한다.
* 모든 하위 시스템(HLE 모듈, 렌더러 백엔드, 오디오 백엔드)은 독립적으로 교체 가능해야 한다.

## Architecture Rules

* The recompiled original code is always the default execution path; repsp stands in only for the firmware and hardware that code calls.
* Replace only the operating system (firmware), system libraries and hardware interfaces with HLE. Sony user-mode PRX modules a game loads from its disc count as firmware libraries and are HLE'd at the NID boundary.
* Every subsystem (HLE modules, the renderer backend, the audio backend) should be replaceable independently.

---

## 개발 철학

여러 구현이 가능할 때는 원본 펌웨어와의 동작 일치를 보존하면서 리버스 엔지니어링 부담을 최소화하는 방법을 선택한다. 런타임 동작은 코드를 읽어서 추론하지 말고 실행해서 측정한다.

최적화보다 정확성을 우선한다.

## Development Philosophy

When multiple implementations are possible, choose the one that preserves behavioral parity with the original firmware while minimizing reverse engineering effort. Measure runtime behavior by running it rather than inferring it from reading code.

Accuracy is preferred over optimization.
