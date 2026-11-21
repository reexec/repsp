# PSP PRX 형식과 재배치 규격

PSP 실행 모듈인 PRX(PSP Relocatable eXecutable)는 평문 ELF32(MIPS LE) 구조를 기반으로 하며, 사용자 모드 PRX의 파일 타입(`e_type`)은 `0xFFA0`(`ET_SCE_EXEC`)입니다. 이 문서는 PRX 모듈 정보, 임포트/익스포트 스터브 구조, 그리고 `SHT_PRXRELOC` 재배치 형식을 정의합니다.

출처:
* [PSP Developer Wiki — PRX](https://www.psdevwiki.com/psp/PRX) (**추정**)
* uOFW `loadcore` 커널 모듈 소스 코드 (**추정**)
* DJMAX Portable (`ULKS-46050`) `BOOT.BIN` 바이너리 측정값 (**확인됨**)

---

## 1. ELF 헤더 및 세그먼트 배치

PSP 평문 사용자 모드 ELF32(MIPS LE, `e_machine = 8`) 실행 파일에는 두 가지 형식이 있습니다.

| 항목 | PRX (`e_type = 0xFFA0`) | 고정 주소 실행 파일 (`e_type = 2`, `ET_EXEC`) |
|---|---|---|
| 링크 주소 | 주소 `0` 기준 상대 주소 | 고정 절대 주소(보통 사용자 메모리 시작 `0x08804000`) |
| 재배치 | `SHT_PRXRELOC`(`0x700000A0`) 적용 | 없음 (`SHT_PRXRELOC` 존재 시 형식 오류; 크기 0인 일반 `SHT_REL`은 무시) |
| 진입점(`e_entry`) | 세그먼트 0 기준 오프셋 (`base + seg[0].vaddr + e_entry`) | 절대 가상 주소 (로드된 `PT_LOAD` 범위 안이어야 함) |
| `.rodata.sceModuleInfo` (`sh_addr`) | 세그먼트 0 기준 오프셋 | 절대 가상 주소 (`sh_addr - image_base`가 버퍼 오프셋) |
| 섹션 헤더 없을 때 `p_paddr` 규약 | 세그먼트 안 오프셋 (**확인됨**) | **파일 오프셋** — `p_vaddr + (p_paddr - p_offset)`으로 가상 주소 환산 (**확인됨**) |
| `SceModuleInfo` 내부 포인터(`gp`, `ent_*`, `stub_*`) | 재배치 후 절대 주소(또는 세그먼트 오프셋) | 링크 시점부터 절대 주소 |

* 프로그램 헤더의 `PT_LOAD` 세그먼트가 메모리에 배치됩니다.
  * PRX는 보통 세그먼트 0(코드/읽기 전용 데이터/스터브)과 세그먼트 1(쓰기 가능 데이터/BSS)로 나뉘며, `ET_EXEC`은 단일 `PT_LOAD` 세그먼트(+ BSS)로 묶이는 경우가 많습니다(**추정**, 소비자 바이너리 1건 **확인됨**).
* 배치 기준 주소: PSP 커널 펌웨어는 첫 사용자 모듈을 `0x08804000`에 배치합니다. `ET_EXEC`의 기준 주소는 가장 낮은 `PT_LOAD`의 `p_vaddr`로 고정되며 다른 주소로 재배치할 수 없습니다.

---

## 2. PRX 재배치 형식 (`SHT_PRXRELOC`)

PRX 파일의 재배치 섹션은 표준 `SHT_REL`(`9`) 대신 `SHT_PRXRELOC`(`0x700000A0`) 타입 태그를 사용합니다. 각 항목은 8바이트 `Elf32_Rel` 구조체입니다:

```c
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;
```

`r_info`의 비트 필드 구성:
* 비트 0~7: 재배치 유형 (`type`)
* 비트 8~15: 오프셋 기준 세그먼트 (`ofsbase`)
* 비트 16~23: 주소 기준 세그먼트 (`addrbase`)

수정 대상 위치:
$$\text{patch\_addr} = \text{base} + \text{seg}[\text{ofsbase}].\text{vaddr} + r\_offset$$

가산 기준 값:
$$S = \text{base} + \text{seg}[\text{addrbase}].\text{vaddr}$$

| 유형 | 이름 | 설명 및 수식 |
|---|---|---|
| 2 | `R_MIPS_32` | 32비트 절대 주소 재배치: $\text{word} \mathrel{+}= S$ |
| 4 | `R_MIPS_26` | 26비트 점프 타깃 재배치 (MIPS `j`/`jal`):<br/>$\text{target} = ((\text{word} \ \& \ \text{0x03FFFFFF}) \ll 2) + S$<br/>$\text{word} = (\text{word} \ \& \ \text{0xFC000000}) \mid ((\text{target} \gg 2) \ \& \ \text{0x03FFFFFF})$ |
| 5 | `R_MIPS_HI16` | MIPS 16비트 상위 재배치 (바로 뒤의 `R_MIPS_LO16`과 쌍):<br/>$\text{addend} = (\text{hi} \ll 16) + (\text{int16})\text{lo}$<br/>$\text{value} = \text{addend} + S$<br/>$\text{new\_hi} = (\text{value} + \text{0x8000}) \gg 16$ |
| 6 | `R_MIPS_LO16` | MIPS 16비트 하위 재배치:<br/>$\text{new\_lo} = (\text{lo} + S) \ \& \ \text{0xFFFF}$ (짝인 `HI16`이 부호 확장에 따른 carry 처리) |

* `R_MIPS_HI16`은 재배치 스트림 상에서 바로 뒤에 나타나는 `R_MIPS_LO16`과 짝을 이룹니다.
* 선행하는 `HI16` 없이 단독으로 나타나는 `LO16`은 자체 addend에 $S$의 하위 16비트를 더하여 갱신합니다.
* `0x700000A1` (압축 재배치)은 repsp에서 지원하지 않으며 거부됩니다.

---

## 3. 모듈 정보 (`.rodata.sceModuleInfo`)

크기는 정확히 52바이트(`0x34`)이며 다음과 같이 배치됩니다:

```c
typedef struct {
    uint16_t attribute;       /* 0x00: 모듈 속성 */
    uint8_t  version[2];      /* 0x02: 모듈 버전 (minor, major) */
    char     name[28];        /* 0x04: 모듈 이름 (28바이트 고정, null 패딩) */
    uint32_t gp_value;        /* 0x20: $gp 초기값 */
    uint32_t ent_top;         /* 0x24: 익스포트 테이블 시작 주소 */
    uint32_t ent_end;         /* 0x28: 익스포트 테이블 끝 주소 */
    uint32_t stub_top;        /* 0x2C: 임포트 스터브 테이블 시작 주소 */
    uint32_t stub_end;        /* 0x30: 임포트 스터브 테이블 끝 주소 */
} SceModuleInfo;
```

---

## 4. 임포트 스터브 테이블 (`sceLibraryStubTable`)

`stub_top`부터 `stub_end`까지 각 라이브러리 임포트 항목이 순차적으로 배치됩니다:

```c
typedef struct {
    uint32_t name_ptr;        /* 0x00: 라이브러리 이름 문자열 주소 */
    uint16_t version;         /* 0x04: 라이브러리 버전 */
    uint16_t attribute;       /* 0x06: 라이브러리 속성 */
    uint8_t  ent_len;         /* 0x08: 엔트리 크기 (워드 단위, 기본 5 = 20바이트) */
    uint8_t  var_count;       /* 0x09: 임포트 변수 수 */
    uint16_t func_count;      /* 0x0A: 임포트 함수 수 */
    uint32_t nid_table;       /* 0x0C: NID 배열 포인터 (func_count * 4바이트) */
    uint32_t stub_table;      /* 0x10: 스터브 thunk 배열 포인터 (func_count * 8바이트) */
} SceLibraryStubTable;
```

* 함수 스터브 하나는 8바이트 크기로, MIPS 점프 또는 시스콜/트랩 명령어 2개로 구성됩니다.

---

## 5. 익스포트 테이블 (`sceLibraryEntryTable`)

`ent_top`부터 `ent_end`까지 각 라이브러리 익스포트 항목이 순차적으로 배치됩니다:

```c
typedef struct {
    uint32_t name_ptr;        /* 0x00: 라이브러리 이름 포인터 (0이면 기본 시스템 익스포트) */
    uint16_t version;         /* 0x04: 라이브러리 버전 */
    uint16_t attribute;       /* 0x06: 속성 */
    uint8_t  ent_len;         /* 0x08: 엔트리 크기 (워드 단위, 기본 4 = 16바이트) */
    uint8_t  var_count;       /* 0x09: 익스포트 변수 수 */
    uint16_t func_count;      /* 0x0A: 익스포트 함수 수 */
    uint32_t table;           /* 0x0C: NID 및 함수/변수 주소 테이블 포인터 */
} SceLibraryEntryTable;
```

* `table` 포인터는 `(func_count + var_count)`개의 NID 배열에 이어 `(func_count + var_count)`개의 가상 주소 배열이 연속으로 배치됩니다.

---

# PSP PRX and `ET_EXEC` format specifications (English)

Plain user-mode PSP executables use ELF32 MIPS little-endian (`e_machine = 8`) in two forms:
* **PRX (`e_type = 0xFFA0`)**: linked at address 0, relocated to a caller-chosen base via `SHT_PRXRELOC` (`0x700000A0`); `e_entry` and `sh_addr` are segment-relative offsets, and without section headers the first program header's `p_paddr` is an offset within the segment (**confirmed**).
* **Fixed-address `ET_EXEC` (`e_type = 2`)**: linked at a fixed virtual address (typically `0x08804000`, the start of PSP user memory) with no relocations (`SHT_PRXRELOC` is invalid; zero-sized ordinary `SHT_REL` sections are ignored). `e_entry`, `sh_addr`, and `SceModuleInfo` pointers are absolute virtual addresses. Without section headers, the first `PT_LOAD`'s `p_paddr` is a **file offset**, mapped to a virtual address as `p_vaddr + (p_paddr - p_offset)` (**confirmed**).

Refer to the sections above for:
1. ELF header, PRX vs `ET_EXEC` comparison, and segment layout
2. `SHT_PRXRELOC` relocation format and calculations (`R_MIPS_32`, `R_MIPS_26`, `R_MIPS_HI16`, `R_MIPS_LO16`)
3. `SceModuleInfo` structure (52 bytes)
4. Import stub table (`SceLibraryStubTable`)
5. Export entry table (`SceLibraryEntryTable`)
