#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

// #define LAB_NET 1

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;
struct spinlock e1000_lock_tx;
struct spinlock e1000_lock_rx;

struct mbuf *tx_mbuf_addr = 0;

extern void net_rx(struct mbuf *m);

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");
  initlock(&e1000_lock_tx, "e1000_tx");
  initlock(&e1000_lock_rx, "e1000_rx");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++)
  {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64)tx_ring;
  if (sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;

  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++)
  {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64)rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64)rx_ring;
  if (sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA + 1] = 0x5634 | (1 << 31);
  // multicast table
  for (int i = 0; i < 4096 / 32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |                 // enable
                     E1000_TCTL_PSP |                // pad short packets
                     (0x10 << E1000_TCTL_CT_SHIFT) | // collision stuff
                     (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8 << 10) | (6 << 20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN |      // enable receiver
                     E1000_RCTL_BAM |     // enable broadcast
                     E1000_RCTL_SZ_2048 | // 2048-byte rx buffers
                     E1000_RCTL_SECRC;    // strip CRC

  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0;       // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0;       // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int e1000_transmit(struct mbuf *m)
{
  // printf("transmitting \n");
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //
  // printf("get e1000_lock\n");
  acquire(&e1000_lock_tx);

  int txindex = regs[E1000_TDT];
  int stat_dd = tx_ring[txindex].status & E1000_TXD_STAT_DD;

  if (stat_dd == 0)
  {
    release(&e1000_lock_tx);
    return -1;
  }

  if (tx_mbuf_addr)
    mbuffree(tx_mbuf_addr);

  tx_ring[txindex].addr = (uint64)(m->head);
  tx_ring[txindex].length = m->len;
  tx_ring[txindex].cmd = E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;

  tx_mbuf_addr = m;

  regs[E1000_TDT] = (regs[E1000_TDT] + 1) % TX_RING_SIZE;

  // printf("release e1000_lock\n");
  release(&e1000_lock_tx);

  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  // printf("get e1000_lock\n");
  acquire(&e1000_lock_rx);

  int rxindex = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
  int sz = 0;

  // printf("call receive\n");
  while (1)
  {
    // printf("scan once\n");
    if (sz == RX_RING_SIZE)
    {
      regs[E1000_RDH] = 0;
      regs[E1000_RDT] = RX_RING_SIZE - 1;
      release(&e1000_lock_rx);
      printf("overflow\n");
      return;
    }
    int stat_dd = rx_ring[rxindex].status & E1000_RXD_STAT_DD;

    if (stat_dd == 0)
      break;

    struct mbuf *m = (struct mbuf *)PGROUNDDOWN(rx_ring[rxindex].addr);
    m->len = rx_ring[rxindex].length;
    net_rx(m);

    struct mbuf *newbuf = mbufalloc(0);
    rx_ring[rxindex].addr = (uint64)newbuf->head;
    rx_ring[rxindex].status = 0;

    rxindex = (rxindex + 1) % RX_RING_SIZE;
    sz += 1;
  }

  rxindex = (rxindex - 1) % RX_RING_SIZE;
  regs[E1000_RDT] = rxindex;

  // printf("release e1000_lock\n");
  release(&e1000_lock_rx);

  return;
}

void e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
