#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "fcntl.h"
// #include "file.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// uint64
// sys_mmap(void) {
//   int prot, fd;
//   int write, read;
//   int length, offset;
//   uint64 addr;
//   struct proc* p;
//   int index;
//   uint64 vma_start;
//   // uint64 vma_end;
//   p = myproc();

//   if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 || argint(2, &prot) < 0
//   ||  argint(3, &fd) < 0 || argint(4, &offset) < 0) {
//     return -1;
//   }

//   read = prot & PROT_READ;
//   write = prot & PROT_WRITE;

//   // permission check
//   if (read && !p->ofile[fd]->readable)
//     return -1;
//   if (write && !p->ofile[fd]->writable)
//     return -1;


//   // find the empty vma entry
//   for (index = 0; index < VMA_NUM; ++index) {
//     if (p->mmap_area[index].ffile == 0)
//       break;
//   }

//   // record info of lazy allocation
//   p->mmap_area[index].length = length;
//   p->mmap_area[index].prot = prot;
//   p->mmap_area[index].ffile = p->ofile[fd];
//   filedup(p->mmap_area[index].ffile);
//   // incrent the ref of file
//   p->mmap_area[index].offset = offset;

//   // change mmap stack
//   vma_start = PGROUNDDOWN(p->mmap_sp - length);
//   p->mmap_area[index].addr = vma_start;
//   p->mmap_sp = vma_start;

//   return vma_start;
// }

uint64 sys_munmap(void) {
  return 1;
}
