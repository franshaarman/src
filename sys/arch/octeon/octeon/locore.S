/*	$OpenBSD: locore.S,v 1.18 2019/05/05 15:43:24 visa Exp $ */

/*
 * Copyright (c) 2001-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/errno.h>
#include <sys/syscall.h>

#include <machine/param.h>
#include <machine/asm.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/regnum.h>
#include <machine/cpustate.h>
#include <octeon/dev/cn30xxcorereg.h>

#include "assym.h"

#define RNG_CONTROL_ADDR	0x9001180040000000
#define RNG_CONTROL_ENABLE	0x3
#define RNG_ENTROPY_ADDR	0x9001400000000000

	.set	noreorder		# Noreorder is default style!
	.set	mips64r2
	.globl	locore_start
	.ent	locore_start, 0
locore_start:
/* initialize ebase */
	dla	t0, 0xffffffff80000000
	mtc0	t0, COP_0_EBASE

/* initialize cvmctl */
	dli     t0, COP_0_CVMCTL_FUSE_START_BIT|COP_0_CVMCTL_NOFDA_CP2|\
		    COP_0_CVMCTL_IPPCI|COP_0_CVMCTL_IPTI
	dmtc0	t0, COP_0_CVMCTL

/* initialize cvmmemctl */
#if 0
	dli	t0, 0x1846104 # If you want to skip write buffer, use this
#else
	dli	t0, 0x46104
#endif
	dmtc0	t0, COP_0_CVMMEMCTL

	mfc0    v0, COP_0_STATUS_REG
	li	v1, ~(SR_INT_ENAB | SR_ERL | SR_EXL)
	and	v0, v1
	mtc0    v0, COP_0_STATUS_REG    # disable all interrupts

	mtc0	zero, COP_0_CAUSE_REG	# Clear soft interrupts

	LA	gp, _gp

	/* Let the init core continue. The others have to wait. */
	bne	a2, zero, 2f
	nop
#if defined(MULTIPROCESSOR)
	rdhwr   t2, $0
	LA	t1, cpu_spinup_mask
1:	ll	t0, 0(t1)
	bne	t2, t0, 1b
	nop
	move	t0, zero
	sc	t0, 0(t1)
	beqz	t0, 1b
	nop
	j	hw_cpu_spinup_trampoline
	nop
#else
	/* Halt extra cores on single-processor kernel. */
1:	wait
	j	1b
	nop
#endif
2:
	/*
	 * Augment the randomdata section using entropy from the RNG.
	 */

	/* Enable the RNG. */
	dli	t0, RNG_CONTROL_ADDR
	ld	t1, (t0)
	ori	t1, RNG_CONTROL_ENABLE
	sd	t1, (t0)

	LA	t0, __kernel_randomdata
	LA	t1, __kernel_randomdata_end
	dli	t2, RNG_ENTROPY_ADDR
1:
	/* Delay to let entropy accumulate. */
	li	v0, 0x1000
2:
	bne	v0, zero, 2b
	subu	v0, v0, 1
	/* Mix entropy. */
	ld	v0, (t0)			# load from randomdata
	ld	v1, (t2)			# load entropy
	xor	v0, v0, v1			# mix entropy
	daddu	t0, t0, 8			# advance ptr
	blt	t0, t1, 1b
	sd	v0, -8(t0)			# store to randomdata

	/*
	 * Clear the compiled BSS segment in OpenBSD code.
	 * U-Boot is supposed to have done this, though.
	 */
	LA	t0, edata
	LA	t1, end
1:
	sd	zero, (t0)
	blt	t0, t1, 1b
	daddu	t0, t0, 8

	/*
	 * Initialize stack and call machine startup.
	 */
	LA	t0, initstack_end - FRAMESZ(CF_SZ)
	PTR_S	ra, CF_RA_OFFS(t0)		# save uboot return address
	PTR_S	sp, 0(t0)			# and stack
	move	sp, t0
	jal	mips_init			# mips_init(argc, argv, envp,
	nop					#    callvec, esym)

	beqz	v0, 1f				# upon failure, return to uboot
	nop

	PTR_S	zero, CF_RA_OFFS(sp)		# Zero out old ra for debugger
	move	sp, v0				# switch to new stack
	jal	main				# main(regs)
	move	a0, zero
	PANIC("Startup failed!")

1:	PTR_L	ra, CF_RA_OFFS(sp)
	PTR_L	sp, 0(sp)
	jr	ra
	nop
	.end	locore_start

#if defined(MULTIPROCESSOR)
LEAF(hw_cpu_spinup_trampoline, 0)
	LA	t0, cpu_spinup_a0
	ld	a0, 0(t0)
	LA	t0, cpu_spinup_sp
	ld	sp, 0(t0)
	jal	hw_cpu_hatch
	nop
END(hw_cpu_spinup_trampoline)
#endif /* MULTIPROCESSOR */

/*
 * Bootstrap stack for mips_init()
 */
	.bss
	.align	3
initstack:
	.space	4096
initstack_end:
