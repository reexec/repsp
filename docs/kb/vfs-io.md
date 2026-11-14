# PSP 파일 경로와 `sceIoOpen` 플래그

이 문서는 PSP `IoFileMgrForUser`의 장치 경로 규약과 open 플래그 정의를 정리한다.

## 1. 장치 경로와 정규화

* **형식**: `device:/path` 또는 `device:path` (`disc0:`, `umd0:`, `ms0:`, `flash0:` 등).
* **경로 정규화**:
  * 역슬래시(`\`)는 `/`로 변환하고 중복 구분자와 `.`, `..`를 정리한다.
  * `..`를 통해 장치 루트 밖으로 벗어나려는 경로는 거부한다 (`SCE_ERROR_ERRNO_ENOENT`).
* **대소문자 구분**: UMD ISO9660 파일시스템은 대문자를 사용하는 반면 타이틀은 소문자로 접근할 수 있으므로, 대소문자를 구분하는 호스트(Linux)에서는 경로 구성 요소별로 대소문자를 무시하고 탐색한다 (**추정**, `iofile_probe`에서 합성 데이터로 동작 **확인됨**).
* **읽기 전용 장치**: `disc0:`와 `umd0:`는 읽기 전용으로 마운트하며 쓰기 플래그가 포함된 open 요청은 `SCE_ERROR_ERRNO_EACCES` (`0x8001000D`)로 거부한다.

## 2. `sceIoOpen` 플래그 (**추정**, [PSPSDK `pspiofilemgr_fcntl.h`](https://github.com/pspdev/pspsdk))

| 플래그 | 값 | 의미 |
|---|---|---|
| `PSP_O_RDONLY` | `0x0001` | 읽기 전용 |
| `PSP_O_WRONLY` | `0x0002` | 쓰기 전용 |
| `PSP_O_RDWR` | `0x0003` | 읽기/쓰기 |
| `PSP_O_APPEND` | `0x0100` | 파일 끝 추가 |
| `PSP_O_CREAT` | `0x0200` | 없으면 생성 |
| `PSP_O_TRUNC` | `0x0400` | 기존 내용 비움 |
| `PSP_O_EXCL` | `0x0800` | `PSP_O_CREAT`와 함께 사용 시 이미 있으면 오류 |

## 3. 64비트 오프셋 인자 규약 (`sceIoLseek` / `sceIoLseekAsync`)

* MIPS o32 ABI에서 `SceOff` (64비트) 인자는 짝수 레지스터 쌍에 정렬되므로 `sceIoLseek(fd, offset, whence)` 호출 시 `$a0 = fd`, `$a1 = 패딩`, `$a2:$a3 = offset`, `$t0 = whence`로 전달되며 64비트 반환값은 `$v0:$v1`에 담긴다 (**추정**, PSPSDK/Allegrex ABI).

---

# PSP File Paths and `sceIoOpen` Flags (English)

This document summarizes PSP `IoFileMgrForUser` device path normalization, case-insensitive lookup on case-sensitive hosts, read-only enforcement on `disc0:`/`umd0:`, `PSP_O_*` open flags (**inferred** from PSPSDK `pspiofilemgr_fcntl.h`), and the MIPS o32 64-bit argument alignment for `sceIoLseek` (`$a0=fd`, `$a2:$a3=offset`, `$t0=whence`, returning `$v0:$v1`). Synthetic VFS behavior is **confirmed** via `iofile_probe`.
