// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct freeMem
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct freeMem cpu_free_list[NCPU];

void kinit()
{
  // initlock(&kmem.lock, "kmem");

  for (int i = 0; i < NCPU; ++i)
  {
    initlock(&cpu_free_list[i].lock, "kmem");
  }
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;
  int cpu_id;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // get cpu id
  push_off();
  cpu_id = cpuid();
  pop_off();

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&cpu_free_list[cpu_id].lock);
  r->next = cpu_free_list[cpu_id].freelist;
  cpu_free_list[cpu_id].freelist = r;
  release(&cpu_free_list[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id;

  push_off();
  cpu_id = cpuid();
  pop_off();

  // not found in this cpu free list
  for (int i = 0; i < NCPU; i++, cpu_id = (cpu_id + 1) % NCPU)
  {
    acquire(&cpu_free_list[cpu_id].lock);
    r = cpu_free_list[cpu_id].freelist;
    if (r)
      cpu_free_list[cpu_id].freelist = r->next;
    release(&cpu_free_list[cpu_id].lock);

    if (r)
      break;
    // found page, break
  }

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}
