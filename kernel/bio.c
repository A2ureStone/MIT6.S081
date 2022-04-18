// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKET_NUM 13
#define BUF_PER_BUCKET 3

// struct
// {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;

struct
{
  struct spinlock lock;
  struct buf buf[BUCKET_NUM][BUF_PER_BUCKET];
  struct spinlock bucket_lock[BUCKET_NUM];

} cache;

uint hashfunc(uint blockno)
{
  return blockno % BUCKET_NUM;
  // hash function for buffer
}

void binit(void)
{
  // struct buf *b;

  initlock(&cache.lock, "bcache.cache spinlock");

  for (int i = 0; i < BUCKET_NUM; ++i)
  {
    for (int j = 0; j < BUF_PER_BUCKET; ++j)
    {
      initsleeplock(&(cache.buf[i][j].lock), "bcache.hash cache");
    }
    initlock(&(cache.bucket_lock[i]), "bcache.bucket spinlock");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint index = hashfunc(blockno);

  acquire(&(cache.bucket_lock[index]));

  // find the target buf in bucket
  for (int i = 0; i < BUF_PER_BUCKET; ++i)
  {
    b = &cache.buf[index][i];
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      b->time_stamp = ticks; // used for lru
      release(&(cache.bucket_lock[index]));
      acquiresleep(&b->lock);
      return b;
    }
  }

  // make new entry for the block
  uint lru_time = __UINT_LEAST32_MAX__;
  uint lru_index = 0;
  uint find = 0;
  for (int i = 0; i < BUF_PER_BUCKET; ++i)
  {
    b = &cache.buf[index][i];
    if (b->refcnt == 0 && b->time_stamp < lru_time)
    {
      lru_index = i;
      lru_time = b->time_stamp;
      find = 1;
    }
  }

  if (!find)
    panic("not find empty place in bucket");

  b = &cache.buf[index][lru_index];
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  release(&(cache.bucket_lock[index]));
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid)
  {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint blockno = b->blockno;
  uint index = hashfunc(blockno);
  acquire(&(cache.bucket_lock[index]));
  b->refcnt--;
  release(&(cache.bucket_lock[index]));
}

void bpin(struct buf *b)
{
  uint blockno = b->blockno;
  uint index = hashfunc(blockno);
  acquire(&(cache.bucket_lock[index]));
  b->refcnt++;
  release(&(cache.bucket_lock[index]));
}

void bunpin(struct buf *b)
{
  uint blockno = b->blockno;
  uint index = hashfunc(blockno);

  acquire(&(cache.bucket_lock[index]));
  b->refcnt--;
  release(&(cache.bucket_lock[index]));
}
