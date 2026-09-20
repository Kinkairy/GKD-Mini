.set noreorder
.set noat
.text
.globl _start
.ent _start

/*
 * U-Boot `go 80600000 menu|both` entry.
 *
 * Return 0 only when the selected active-low GPIO-B key remains asserted for
 * the complete debounce window.  U-Boot turns a nonzero return into command
 * failure, which lets `run` fall through to A without a shell `if` command.
 */
_start:
	/* Default to MENU/GPB20.  argv[1][0] == 'b' adds volume+/GPB11. */
	lui	$t2, 0x0010
	sltiu	$t0, $a0, 2
	bnez	$t0, 1f
	nop
	lw	$t1, 4($a1)
	lbu	$t1, 0($t1)
	addiu	$t0, $zero, 98
	bne	$t1, $t0, 1f
	nop
	ori	$t2, $t2, 0x0800
1:
	/*
	 * X1830 uses a 0x1000 port stride and shadowed pin configuration.
	 * Configure the selected GPIO-B bits as inputs without changing pulls:
	 * INT clear, MSK set, PAT1 set, then load GPIO group 1.
	 */
	lui	$t0, 0xb001
	ori	$t0, $t0, 0x7000
	sw	$t2, 0x0018($t0)
	sw	$t2, 0x0024($t0)
	sw	$t2, 0x0034($t0)
	addiu	$t1, $zero, 1
	sw	$t1, 0x00f0($t0)
	sync

	/* X1830 GPIO-B PXPIN: physical 0x10011000, uncached KSEG1. */
	lui	$t0, 0xb001
	ori	$t0, $t0, 0x1000
	mfc0	$t3, $9
2:
	sync
	lw	$t4, 0($t0)
	and	$t4, $t4, $t2
	beq	$t4, $t2, 3f
	nop
	mfc0	$t5, $9
	subu	$t5, $t5, $t3
	lui	$t6, 0x017d
	ori	$t6, $t6, 0x7840
	sltu	$t7, $t5, $t6
	bnez	$t7, 2b
	nop
	move	$v0, $zero
	jr	$ra
	nop
3:
	addiu	$v0, $zero, 1
	jr	$ra
	nop

.end _start
