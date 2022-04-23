//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

extern pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);

struct devsw devsw[NDEV];
struct
{
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file *
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for (f = ftable.file; f < ftable.file + NFILE; f++)
  {
    if (f->ref == 0)
    {
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0)
  {
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_PIPE)
  {
    pipeclose(ff.pipe, ff.writable);
  }
  else if (ff.type == FD_INODE || ff.type == FD_DEVICE)
  {
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_INODE || f->type == FD_DEVICE)
  {
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if (f->readable == 0)
    return -1;

  if (f->type == FD_PIPE)
  {
    r = piperead(f->pipe, addr, n);
  }
  else if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  }
  else
  {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if (f->writable == 0)
    return -1;

  if (f->type == FD_PIPE)
  {
    ret = pipewrite(f->pipe, addr, n);
  }
  else if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n)
    {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if (r != n1)
      {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  }
  else
  {
    panic("filewrite");
  }

  return ret;
}

int lazy_alloc_mmap(uint64 addr, uint64 scause)
{
  // return 0;
  struct proc *p = myproc();
  uint access_flag = 0;
  uint64 st, ed;
  void *mem;
  int offset;
  int i;
  int bytes = PGSIZE;

  for (i = 0; i < VMA_NUM; ++i)
  {
    if (p->mmap_area[i].ffile == 0)
      continue;
    st = p->mmap_area[i].addr;
    ed = st + p->mmap_area[i].length;
    if (st <= addr && addr < ed)
      break;
  }

  // illegal page
  if (i == VMA_NUM)
    return -1;

  if (scause == 13)
    access_flag = PROT_READ;
  if (scause == 15)
    access_flag = PROT_WRITE;
  // check for permission
  if (!(access_flag & p->mmap_area[i].prot))
    return -1;

  addr = PGROUNDDOWN(addr);
  // if memory out, return -1
  if ((mem = kalloc()) == 0)
    return -1;

  memset(mem, 0, PGSIZE);

  offset = p->mmap_area[i].offset + addr - st;
  if (ed - addr < PGSIZE)
    bytes = ed - addr;

  // read file content to memory
  ilock(p->mmap_area[i].ffile->ip);
  if (readi(p->mmap_area[i].ffile->ip, 0, (uint64)mem, offset, bytes) <= 0)
  {
    iunlock(p->mmap_area[i].ffile->ip);
    return -1;
  }
  iunlock(p->mmap_area[i].ffile->ip);

  // map to the process pagetable
  if (mappages(p->pagetable, addr, PGSIZE, (uint64)mem, p->mmap_area[i].prot << 1 | PTE_U) != 0)
  {
    kfree(mem);
    return -1;
  }

  // mark this page allocated
  p->mmap_area[i].pg[(addr - p->mmap_area[i].addr) / PGSIZE] = 1;
  p->mmap_area[i].alcat_pg_nums += 1;

  return 0;
}

uint64
sys_mmap(void)
{
  // return -1;
  int prot, fd, flags;
  int write, read;
  int length, offset;
  uint64 addr;
  struct proc *p;
  int index;
  uint64 vma_start;
  // uint64 vma_end;
  p = myproc();

  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0 || argint(3, &flags) < 0 || argint(4, &fd) < 0 || argint(5, &offset))
  {
    return -1;
  }

  read = prot & PROT_READ;
  write = prot & PROT_WRITE;

  // permission check

  // find the empty vma entry
  for (index = 0; index < VMA_NUM; ++index)
  {
    if (p->mmap_area[index].ffile == 0)
      break;
  }

  printf("index: %d\n", index);

  if (flags == MAP_SHARED)
  {
    // if MAP_SHARED, check permission with the original file
    if (read && !p->ofile[fd]->readable)
      return -1;
    if (write && !p->ofile[fd]->writable)
      return -1;
  }

  // record info of lazy allocation
  p->mmap_area[index].length = length;
  p->mmap_area[index].prot = prot;
  p->mmap_area[index].flags = flags;
  p->mmap_area[index].ffile = p->ofile[fd];
  filedup(p->mmap_area[index].ffile);
  // incrent the ref of file
  p->mmap_area[index].offset = offset;

  // change mmap stack
  vma_start = PGROUNDDOWN(p->mmap_sp - length);
  p->mmap_area[index].addr = vma_start;

  int nums = (p->mmap_sp - vma_start) / PGSIZE;
  if (nums > 5)
    panic("mmap pages larger than 5");
  p->mmap_area[index].pg_nums = nums;

  p->mmap_sp = vma_start;

  return vma_start;
}

uint64 sys_munmap(void)
{
  uint64 addr, nums;
  int length;
  uint64 st, ed;
  struct proc *p = myproc();

  if (argaddr(0, &addr) < 0 || argint(1, &length) < 0)
  {
    return -1;
  }

  // if (addr % PGSIZE != 0)
  //   panic("the unmap addr is not multiple of PGSIZE");

  // __sync_synchronize();

  int id;
  for (id = 0; id < VMA_NUM; id++)
  {
    if (p->mmap_area[id].ffile == 0)
    {
      continue;
    }
    st = p->mmap_area[id].addr;
    ed = st + p->mmap_area[id].length;
    if (st <= addr && addr < ed)
    {
      break;
    }
  }

  // __sync_synchronize();

  if (id == VMA_NUM)
    panic("munmap: cannot find the vma entry");


  // for (uint64 start = addr; start < addr + length; start += PGSIZE) {
  //   // error version, just check if one of the page not mapped, return
  //   pte_t* pte = walk(p->pagetable, start, 0);
  //   if (pte == 0)
  //     return 0;
  // }


  // write the page back to the file
  if (p->mmap_area[id].flags == MAP_SHARED)
  {
    for (uint64 start = addr; start < addr + length; start += PGSIZE)
    {
      uint write_bytes = PGSIZE;
      uint offset = p->mmap_area[id].offset + start - p->mmap_area[id].addr;
      pte_t *pte = walk(p->pagetable, addr, 0);
      if (pte == 0)
        panic("munmap: pte should exist");

      int dirty = PTE_FLAGS(*pte) & PTE_D;

      if (dirty)
      {
        // this is wrong when you need to append file
        if (start + PGSIZE > p->mmap_area[id].addr + p->mmap_area[id].length)
          write_bytes = p->mmap_area[id].addr + p->mmap_area[id].length - start;

        begin_op();
        ilock(p->mmap_area[id].ffile->ip);
        printf("write addr: %p, write bytes: %d\n", start, write_bytes);
        if (writei(p->mmap_area[id].ffile->ip, 1, start, offset, write_bytes) != write_bytes)
          panic("write error");
        iunlock(p->mmap_area[id].ffile->ip);
        end_op();
      }
    }
  }

  nums = (PGROUNDUP(addr + length) - addr) / PGSIZE;
  uvmunmap(p->pagetable, addr, nums, 1);
  p->mmap_area[id].alcat_pg_nums -= nums;

  return 0;
}