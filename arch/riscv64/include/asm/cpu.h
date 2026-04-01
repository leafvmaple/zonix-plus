#pragma once

/*
 * RISC-V supervisor CSR bit definitions (cpu.h)
 *
 * Only the fields relevant to kernel S-mode operation are listed.
 */

#ifndef __ASSEMBLY__

#include <base/types.h>

/* sstatus — Supervisor Status Register */
inline constexpr uint64_t SSTATUS_SIE = 1ULL << 1;  /* supervisor interrupt enable  */
inline constexpr uint64_t SSTATUS_SPIE = 1ULL << 5; /* previous interrupt enable    */
inline constexpr uint64_t SSTATUS_UBE = 1ULL << 6;  /* U-mode big-endian            */
inline constexpr uint64_t SSTATUS_SPP = 1ULL << 8;  /* previous privilege (0=U,1=S) */
inline constexpr uint64_t SSTATUS_SUM = 1ULL << 18; /* permit supervisor user access */
inline constexpr uint64_t SSTATUS_MXR = 1ULL << 19; /* make executable readable     */
inline constexpr uint64_t SSTATUS_SD = 1ULL << 63;  /* FS/XS dirty summary          */

/* sstatus bits that must be set when returning to user mode */
inline constexpr uint64_t SSTATUS_USER = SSTATUS_SPIE; /* SPIE=1 so irqs re-enable on sret to U */

/* sstatus bits for a kernel thread returning from interrupt */
inline constexpr uint64_t SSTATUS_KERN = SSTATUS_SPP | SSTATUS_SPIE;

/* sie — Supervisor Interrupt-Enable Register */
inline constexpr uint64_t SIE_SSIE = 1ULL << 1; /* software interrupt enable  */
inline constexpr uint64_t SIE_STIE = 1ULL << 5; /* timer interrupt enable     */
inline constexpr uint64_t SIE_SEIE = 1ULL << 9; /* external interrupt enable  */

/* sip — Supervisor Interrupt-Pending Register */
inline constexpr uint64_t SIP_SSIP = 1ULL << 1;
inline constexpr uint64_t SIP_STIP = 1ULL << 5;
inline constexpr uint64_t SIP_SEIP = 1ULL << 9;

#endif /* !__ASSEMBLY__ */
