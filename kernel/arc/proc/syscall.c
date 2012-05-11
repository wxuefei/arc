/*
 * Copyright (c) 2011-2012 Graham Edgecombe <graham@grahamedgecombe.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <arc/proc/syscall.h>
#include <arc/cpu/flags.h>
#include <arc/cpu/efer.h>
#include <arc/cpu/msr.h>
#include <arc/cpu/gdt.h>
#include <arc/tty.h>

// TODO: move to separate file and use real function pointer syntax
uint64_t syscall_table_size = 1;
uint64_t syscall_table[1] = { (uint64_t) &tty_puts };

void syscall_init(void)
{
  /* set the SYSCALL and SYSRET selectors */
  uint64_t star = 0;
  star |= ((uint64_t) (SLTR_KERNEL_DATA | RPL3)) << 48;
  star |=  (uint64_t) SLTR_KERNEL_CODE           << 32;
  msr_write(MSR_STAR, star);// (((uint64_t) SLTR_KERNEL_DATA | RPL3) << 48) | (((uint64_t) SLTR_KERNEL_CODE) << 32));

  /*
   * how the selectors are determined:
   *
   * SYSRET:
   *   CS = sysret_sel + 16
   *   SS = sysret_sel + 8
   *
   * SYSCALL:
   *   CS = syscall_sel
   *   SS = syscall_sel + 8
   *
   * Required GDT layout:
   *
   *    0: null
   *    8: kernel code (syscall_sel)
   *   16: kernel data (sysret_sel)
   *   24: user data
   *   32: user code
   *   (additional entries e.g. TSS at the end)
   */

  /* set the long mode SYSCALL target RIP */
  msr_write(MSR_LSTAR, (uint64_t) &syscall_stub);

  /*
   * set the flags to clear upon a SYSCALL:
   *
   *   - DF is cleared, the AMD64 System V ABI states this must be done before
   *     a function call
   *
   *   - IF is cleared, this is to avoid a race condition where an interrupt
   *     happens in supervisor mode before RSP and GS have been set (if an
   *     interrupt did happen, RSP0 would not be loaded into RSP from the TSS
   *     and the interrupt would run in the user stack, and the GS stuff would
   *     get messy)
   */
  msr_write(MSR_SFMASK, FLAGS_DF | FLAGS_IF);

  /* enable the SCE flag in the EFER register */
  efer_write(efer_read() | EFER_SCE);
}

