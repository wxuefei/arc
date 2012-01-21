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

#include <arc/intr/ic.h>
#include <arc/intr/pic.h>
#include <arc/intr/lapic.h>
#include <arc/intr/lx2apic.h>
#include <arc/panic.h>
#include <stdarg.h>

static int ic_type = IC_TYPE_NONE;

void ic_bsp_init(int type, ...)
{
  va_list args;
  va_start(args, type);

  switch (type)
  {
    case IC_TYPE_PIC:
      pic_init();
      break;

    case IC_TYPE_LAPIC:
      lapic_mmio_init(va_arg(args, uint64_t));
      break;

    case IC_TYPE_LX2APIC:
      break;

    default:
      panic("unknown ic type");
      break;
  }

  ic_type = type;
  va_end(args);
}

void ic_ap_init(void)
{
  switch (ic_type)
  {
    case IC_TYPE_NONE:
      panic("no IC initialised");
      break;

    case IC_TYPE_LAPIC:
      lapic_init();
      break;

    case IC_TYPE_LX2APIC:
      lx2apic_init();
      break;
  }
}

void ic_ack(intr_id_t id)
{
  switch (ic_type)
  {
    case IC_TYPE_NONE:
      panic("no IC initialised");
      break;

    case IC_TYPE_PIC:
      if (id >= IRQ0 && id <= IRQ15)
        pic_ack(id);
      break;

    case IC_TYPE_LAPIC:
      if (id >= IRQ0 && id <= IRQ23)
        lapic_ack();
      break;

    case IC_TYPE_LX2APIC:
      if (id >= IRQ0 && id <= IRQ23)
        lx2apic_ack();
      break;
  }
}

void ic_ipi_init(cpu_lapic_id_t id)
{
  switch (ic_type)
  {
    case IC_TYPE_NONE:
      panic("no IC initialised");
      break;

    case IC_TYPE_PIC:
      panic("8259 PICs do not support IPIs");
      break;

    case IC_TYPE_LAPIC:
      lapic_ipi(id, 0x05, 0x00);
      break;

    case IC_TYPE_LX2APIC:
      lx2apic_ipi(id, 0x05, 0x00);
      break;
  }
}

void ic_ipi_startup(cpu_lapic_id_t id, uint8_t trampoline_addr)
{
  switch (ic_type)
  {
    case IC_TYPE_NONE:
      panic("no IC initialised");
      break;

    case IC_TYPE_PIC:
      panic("8259 PICs do not support IPIs");
      break;

    case IC_TYPE_LAPIC:
      lapic_ipi(id, 0x06, trampoline_addr);
      break;

    case IC_TYPE_LX2APIC:
      lx2apic_ipi(id, 0x06, trampoline_addr);
      break;
  }
}

