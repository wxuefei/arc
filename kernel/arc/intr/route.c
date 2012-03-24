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

#include <arc/intr/route.h>
#include <arc/intr/pic.h>
#include <arc/intr/apic.h>
#include <arc/intr/ioapic.h>
#include <arc/lock/rwlock.h>
#include <arc/lock/intr.h>
#include <arc/smp/mode.h>
#include <arc/util/list.h>
#include <arc/tty.h>
#include <arc/panic.h>
#include <stdlib.h>

typedef struct intr_handler_node
{
  intr_handler_t handler;
  list_node_t node;
} intr_handler_pair_t;

static rwlock_t intr_route_lock = RWLOCK_UNLOCKED;
static list_t intr_handlers[INTERRUPTS];

void intr_dispatch(intr_state_t *state)
{
  /* acknowledge we received this interrupt if it came from the APIC or PIC */
  intr_t intr = state->id;
  if (smp_mode == MODE_SMP)
  {
    if (intr > FAULT31 && intr != SPURIOUS)
      apic_ack();
  }
  else
  {
    if (intr >= IRQ0 && intr <= IRQ15)
      pic_ack(intr - IRQ0);
  }

  /* if there is no handler panic (this is for debugging purposes) */
  rw_rlock(&intr_route_lock);
  list_t *handler_list = &intr_handlers[state->id];
  if (handler_list->size == 0)
    panic("unhandled interrupt %d", state->id);

  /* call all the handlers */
  list_for_each(handler_list, node)
  {
    intr_handler_pair_t *pair = container_of(node, intr_handler_pair_t, node);

    intr_handler_t handler = pair->handler;
    (*handler)(state);
  }
  rw_runlock(&intr_route_lock);
}

static bool _intr_route_intr(intr_t intr, intr_handler_t handler)
{
  /* allocate the handler pair */
  intr_handler_pair_t *pair = malloc(sizeof(*pair));
  if (!pair)
    return false;

  /* fill it out */
  pair->handler = handler;

  /* adds it to the handler list */
  list_add_tail(&intr_handlers[intr], &pair->node);
  return true;
}

static void _intr_unroute_intr(intr_t intr, intr_handler_t handler)
{
  /* find the handler pair */
  list_for_each(&intr_handlers[intr], node)
  {
    intr_handler_pair_t *pair = container_of(node, intr_handler_pair_t, node);
    if (pair->handler == handler)
    {
      /* unlink it from the list */
      list_remove(&intr_handlers[intr], &pair->node);

      /* free it */
      free(pair);
      return;
    }
  }
}

bool intr_route_intr(intr_t intr, intr_handler_t handler)
{
  intr_lock();
  rw_wlock(&intr_route_lock);
  bool ok = _intr_route_intr(intr, handler);
  rw_wunlock(&intr_route_lock);
  intr_unlock();
  return ok;
}

void intr_unroute_intr(intr_t intr, intr_handler_t handler)
{
  intr_lock();
  rw_wlock(&intr_route_lock);
  _intr_unroute_intr(intr, handler);
  rw_wunlock(&intr_route_lock);
  intr_unlock();
}

bool intr_route_irq(irq_tuple_t *tuple, intr_handler_t handler)
{
  /* obtain the write lock */
  intr_lock();
  rw_wlock(&intr_route_lock);

  /* check if we are in uni-processor or SMP mode */
  if (smp_mode == MODE_UP)
  {
    /* calculate the interrupt number, only IRQ0-15 can be routed */
    irq_t irq = tuple->irq;
    if (irq < 16)
    {
      intr_t intr = irq + IRQ0;

      /* actually route the interrupt */
      bool ok = _intr_route_intr(intr, handler);

      /* unmask the interrupt in the PIC */
      pic_unmask(irq);

      /* release the write lock */
      rw_wunlock(&intr_route_lock);
      intr_unlock();
      return ok;
    }
  }
  else
  {
    /* calculate the interrupt number */
    irq_t irq = tuple->irq;
    intr_t intr = (irq % IRQS) + IRQ0;

    /* iterate through the I/O APICs */
    list_for_each(&ioapic_list, node)
    {
      ioapic_t *apic = container_of(node, ioapic_t, node);

      /* check if the IRQ belongs to this I/O APIC */
      irq_t irq_first = apic->irq_base;
      irq_t irq_last = apic->irq_base + apic->irqs - 1;
      if (irq >= irq_first && irq < irq_last)
      {
        /* route the interrupt */
        bool ok = _intr_route_intr(intr, handler);

        /* program the I/O APIC */
        ioapic_route(apic, tuple, intr);

        /* all done! */
        rw_wunlock(&intr_route_lock);
        intr_unlock();
        return ok;
      }
    }
  }

  /* release the write lock */
  rw_wunlock(&intr_route_lock);
  intr_unlock();
  return false;
}

void intr_unroute_irq(irq_tuple_t *tuple, intr_handler_t handler)
{
  /* obtain the write lock */
  intr_lock();
  rw_wlock(&intr_route_lock);

  /* check if we are in uni-processor or SMP mode */
  if (smp_mode == MODE_UP)
  {
    /* calculate the interrupt number, only IRQ0-15 can be unrouted */
    irq_t irq = tuple->irq;
    if (irq < 16)
    {
      intr_t intr = irq + IRQ0;

      /* actually unroute the interrupt */
      _intr_unroute_intr(intr, handler);

      /* mask the interrupt in the PIC if it is no longer used */
      if (intr_handlers[intr].size == 0)
        pic_mask(irq);
    }
  }
  else
  {
    /* calculate the interrupt number */
    irq_t irq = tuple->irq;
    intr_t intr = (irq % IRQS) + IRQ0;

    /* iterate through the I/O APICs */
    list_for_each(&ioapic_list, node)
    {
      ioapic_t *apic = container_of(node, ioapic_t, node);

      /* check if the IRQ belongs to this I/O APIC */
      irq_t irq_first = apic->irq_base;
      irq_t irq_last = apic->irq_base + apic->irqs - 1;
      if (irq >= irq_first && irq < irq_last)
      {
        /* program the I/O APIC */
        ioapic_mask(apic, tuple);
      }
    }

    /* unroute this interrupt */
    _intr_unroute_intr(intr, handler);
  }

  /* release the write lock */
  rw_wunlock(&intr_route_lock);
  intr_unlock();
}

