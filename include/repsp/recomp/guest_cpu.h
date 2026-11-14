#ifndef REPSP_RECOMP_GUEST_CPU_H
#define REPSP_RECOMP_GUEST_CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct repsp_guest_cpu_ops
{
    uint32_t context_size;                        /* bytes of one saved context */
    void (*save)(void* context);                  /* live register file -> context */
    void (*restore)(const void* context);         /* context -> live register file */
    uint32_t (*get_gpr)(int index);               /* MIPS GPR 0..31 */
    void (*set_gpr)(int index, uint32_t value);
    void (*call_guest)(uint32_t address);         /* run a guest function until it returns */
    uint32_t (*get_fpr)(int index);               /* MIPS COP1 FPR 0..31 bit pattern */
    void (*set_fpr)(int index, uint32_t value);
} repsp_guest_cpu_ops;

void repsp_recomp_register_cpu(const repsp_guest_cpu_ops* ops);
const repsp_guest_cpu_ops* repsp_recomp_get_cpu_ops(void);

void repsp_recomp_hle_call(uint32_t nid);         /* an import thunk was called */
void repsp_recomp_syscall(uint32_t code);         /* a raw syscall instruction */
void repsp_recomp_poll(void);                     /* safe point: deliver pending interrupts */

/* C-callable guest memory access helpers */
uint8_t* repsp_guest_base(void);
uint32_t repsp_guest_mask(void);
uint8_t* repsp_guest_translate(uint32_t addr);

uint8_t repsp_guest_read8(uint32_t addr);
uint16_t repsp_guest_read16(uint32_t addr);
uint32_t repsp_guest_read32(uint32_t addr);
uint64_t repsp_guest_read64(uint32_t addr);

void repsp_guest_write8(uint32_t addr, uint8_t val);
void repsp_guest_write16(uint32_t addr, uint16_t val);
void repsp_guest_write32(uint32_t addr, uint32_t val);
void repsp_guest_write64(uint32_t addr, uint64_t val);

#ifdef __cplusplus
}
#endif

#endif /* REPSP_RECOMP_GUEST_CPU_H */
