#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "elf.h"
#include "fs.h"
#include "file.h"
#include "mman.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

int mmapAssignProc(struct proc *currproc, struct mmapInfo *vma);

// General map cache
struct {
  // struct spinlock lock;
  struct legend2 maps[NMAPS];
} mmapCache;

// Anonymous map cache
struct {
  struct anon anonMaps[NMAPS];
} anonCache;

// mmapInfo structs
struct {
  struct mmapInfo info[NMAPS];
} mtable;

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++){
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// MINE: Get details of the pte
void getDetails(pte_t *pte){
  // cprintf("%d %d %d %d %d %d\n", *pte & PTE_P, *pte & PTE_W, *pte & PTE_U, *pte & 0x8, *pte & 0x20, *pte & 0x40);
}

int isDirty(pte_t *pte){
  return *pte & 0x40;
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

int isMapped(char *v){
  // cprintf("%x\n", v);
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
// TODO: Don't free the pages containing our maps if the reference count is greater than 1
void
freevm(pde_t *pgdir)
{
  uint i;
  
  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  
  // If this address lies in the range of a map, handle it by pointing mem to the correct page. Or best, don't map it only. Let it page fault. Ensure but pte is with correct permissions. Map Private should be marked as Read-only, that's it. Set the AVL bit to some value, we have three bits for the purpose. Let's set them to an alternating pattern of 101
  // Actual flags of VMA never change, so processes can share VMAs as well. Need refCount on vma
  // Nullify pte
  struct proc *currproc = myproc();
  int pid = currproc->pid;
  if (pid != 1 && pid!=2){
    for (int i=0; i<NOMAPS; i++){
      struct mmapInfo *vma = currproc->maps[i];
      // Get a new VMA, set the appropriate flags
      if (vma && vma->ref){
        // Get the pte, mark the AVL bits]
        char *start = vma->start;
        char *end = (char *) PGROUNDUP((uint) vma->end);
        char *a = start;
        for (; a < end; a += PGSIZE){
          if ( (pte = walkpgdir(pgdir, a, 0)) == 0){
            panic("copyuvm: pte should exist");
          }
          if (!(*pte & PTE_P)){
            continue;
          }
          *pte |= PTE_AVL;
          *pte &= ~PTE_P;
        }
      }
    }
  }

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P)){
      if (*pte & PTE_AVL){
          *pte |=  PTE_P;
          *pte &= ~PTE_AVL;
          continue;
      }
      panic("copyuvm: page not present");
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

char *extend(struct proc *p, uint oldsz, uint newsz){
  p->sz += newsz;
  return (char *) oldsz;
}

struct mmapInfo *mmapdup(struct mmapInfo *m){
  if (m->pages && m->pages->f->ref < 1){
    panic("filedup");
  } else if (m->pages){
    m->pages->f->ref++;
    m->pages->mapRef++;
  } else{
    m->anonMaps->ref++;
  }
  m->ref++;
  return m;
}

// Print all the maps of the process
void unfoldMaps(){
}

void mmapCopyUvm(pde_t *pgdir, struct mmapInfo *vma){

}

// VMA carries the entire information of how we want to map the pages
int mmapAllocUvm(pde_t *pgdir, struct mmapInfo *vma, int reload, char *faultedAddr){
  char *mem;
  uint a;
  int mappingPagesFrom;
  struct legend2 *m = vma->pages;
  struct anon *am = vma->anonMaps;
  char **physicalPages = 0;
  int anonFlag = 0;
  char *end, *start;

  if (m){
    physicalPages = m->physicalPages;
  } else if (am){
    physicalPages = am->physicalPages;
  } else{
    panic("Incorrect VMA");
  }

  if (!faultedAddr){
    mappingPagesFrom = vma->firstPageIndex;
    a = (uint) vma->start;
    end = vma->end;
    start = vma->start;
  } else{
    mappingPagesFrom = (uint) (faultedAddr - vma->start) / PGSIZE;
    a = (uint) faultedAddr;
    end = faultedAddr + PGSIZE;
    start = faultedAddr;
  }

  for(; a < (uint) end; a += PGSIZE){
    if (reload || !physicalPages[mappingPagesFrom]){
      if (!physicalPages[mappingPagesFrom] && am && (vma->prot & PROT_WRITE)){
        anonFlag = PTE_W;
      }
      mem = kalloc();
      if(mem == 0){
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, (uint) end, (uint) start);
        return -1;
      }
    }
    if (reload){
      pte_t *pte;
      if ( (pte = walkpgdir(pgdir, (void *) a, 0)) == 0){
        panic("Drunken walk\n");
      }
      *pte = 0;
      memmove(mem, physicalPages[mappingPagesFrom], PGSIZE);
    }
    if (!reload && !physicalPages[mappingPagesFrom]){
      memset(mem, 0, PGSIZE);
      physicalPages[mappingPagesFrom] = mem;
    } else if (!reload && physicalPages[mappingPagesFrom]){
      mem = physicalPages[mappingPagesFrom];
    }
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), vma->actualFlags | anonFlag) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, (uint) end, (uint) start);
      kfree(mem);
      return -1;
    }
    mappingPagesFrom++;
  }

  return 0;
}

struct legend2 *mmapAlloc(){
  for (int i=0; i<NMAPS; i++){
    if (mmapCache.maps[i].mapRef == 0){
      return &mmapCache.maps[i];
    }
  }
  return 0;
}

struct mmapInfo *mmapAssign(struct legend2 *m, struct anon *am, char *start, int offset, int length, int prot, int flags){
  int numPages = (PGROUNDUP(offset + length) - PGROUNDDOWN(offset)) / PGSIZE;

  struct mmapInfo *vma = 0;
  for (int i=0; i<NMAPS; i++){
    if (mtable.info[i].ref==0){
      vma = &mtable.info[i];
      break;
    }
  }

  if (!vma){
    return 0;
  }

  vma->start = start;
  vma->end = vma->start + offset + length - PGROUNDDOWN(offset);
  vma->pages = m;
  vma->ref = 1;
  vma->firstPageIndex = (PGROUNDDOWN(offset)) / PGSIZE;
  vma->numPages = numPages;
  vma->prot = prot;
  vma->flags = flags;
  vma->anonMaps = am;

  // If MAP_PRIVATE is present, mark the page as read-only. When page fault occurs, then we'll decide if the page was actually writable or not using the prot field
  if (flags & MAP_PRIVATE){
    vma->actualFlags = PTE_P | PTE_U;
  } else{
    if (prot & PROT_WRITE){
      vma->actualFlags = PTE_P | PTE_W | PTE_U;
    } else{
      vma->actualFlags = PTE_P | PTE_U;
    }
  }

  return vma;
}

int min(int a, int b){
  return a<b?a:b;
}

char *getPTE(char *start){
  return (char *) PTE_ADDR(walkpgdir(myproc()->pgdir, start, 0));
}

void printInfo(struct legend2 *m){
  for (int i=0; i<MAX_PAGES; i++){
    cprintf("%d: %x\n", i, m->physicalPages[i]);
  }
}

// Map the pages into struct legend2 only if they could be loaded
int mmapLoadUvm(pde_t *pgdir, struct legend2 *map, struct mmapInfo *m, int reload, char *faultedAddr){
  // pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz
  // cprintf("Offset: %d | i: %d | offset + i: %d\n", offset, i, offset + i);a
  uint i, pa, n, sz, offset;
  pte_t *pte;
  int pageNum;
  char *addr;
  int firstPageIndex, numPages;

  if (!faultedAddr){
    addr = m->start;
    sz = min(m->numPages*PGSIZE, map->f->ip->size - m->firstPageIndex*PGSIZE);
    offset = m->firstPageIndex * PGSIZE;
    pageNum = m->firstPageIndex;
  } else{
    addr = faultedAddr;
    firstPageIndex = (int) (faultedAddr - m->start) / PGSIZE;
    numPages = 1;
    sz = min(numPages*PGSIZE, map->f->ip->size - firstPageIndex*PGSIZE);
    offset = firstPageIndex * PGSIZE;
    pageNum = firstPageIndex;
  }

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    // cprintf("Loaduvm address: %x\n", P2V(PTE_ADDR(*pte)));
    pa = PTE_ADDR(*pte);
    // If the page was not in the cache
    if (!map->ref[pageNum]){
      if(sz - i < PGSIZE)
        n = sz - i;
      else
        n = PGSIZE;
      int written;
      if(( written = readi(map->f->ip, P2V(pa), offset+i, n) ) != n){
        return -1;
      }
    }
    map->ref[pageNum]++;
    // cprintf("pageNum: %d = %d\n", pageNum, map->ref[pageNum]);
    pageNum++;
  }
  return 0;
}

struct legend2 *findInCache(struct file *f){
  struct legend2 *old;
  struct legend2 *m = 0;

  for (int i=0; i<NMAPS; i++){
    if ( (old = &mmapCache.maps[i])->mapRef != 0){
      if (old->f->ip == f->ip){
        m = &mmapCache.maps[i];
        break;
      }
    }
  }

  return m;
}

void freeVMA(struct mmapInfo *vma){
  vma->start = 0;
  vma->end = 0;
  vma->firstPageIndex = -1;
  vma->flags = 0;
  vma->prot = 0;
  vma->numPages = 0;
  vma->pages = 0;
  vma->ref = 0;
  vma->anonMaps = 0;
}

int readFromPageCache(struct legend2 *m, char *addr, int offset, int length){
  // Reworking the entire code to behave like writeToPageCache
  // So the issue is that ls reads an inode of 512 bytes, which doesn't actually contain 512 bytes but some 364 or something
  // Now, read should stop once we reach the file size
  // So, we check which page does the last byte belong to
  // If we are on the current page, then we can atmost read sz bytes from the page, so you should consider that too
  char *pagePointer;
  int pageIndex, toRead, canRead;


  if (offset >= m->f->ip->size){
    return 0;
  }

  pageIndex = PGROUNDDOWN(offset) / PGSIZE;
  // fileLastPage = PGROUNDDOWN(m->f->ip->size) / PGSIZE;
  // lastPageBytes = (m->f->ip->size) % PGSIZE;
  toRead = length;
  cprintf("Offset: %d | Length: %d | File size: %d | %d | Need to write %d bytes\n", offset, length, m->f->ip->size, pageIndex, toRead);

  offset = offset % PGSIZE;

  while (toRead){
    pagePointer = m->physicalPages[pageIndex];

    if (!pagePointer){
      panic("732 Unmapped\n");
    }

    pagePointer += offset;

    canRead = min(toRead, PGSIZE - offset);
    memmove(addr, pagePointer, canRead);
    if (addr[0]=='\0'){
      break;
    }

    toRead = toRead - canRead;
    offset = (offset + canRead) % PGSIZE;

    if (offset + PGSIZE*pageIndex > m->f->ip->size){
      break;
    }

    addr = addr + canRead;
    pageIndex++;
  }

  return length - toRead;
}

int mmapdeallocuvm(pde_t *pgdir, uint oldsz, uint newsz){
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      // char *v = P2V(pa);
      // kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

struct legend2 *readIntoPageCache(void *addr, unsigned int length, int prot, int flags, struct file *f, int offset){
  struct proc *currproc = myproc();
  struct legend2 *m = 0;
  int shouldLoad = 1;

  // Find in the global cache
  m = findInCache(f);

  if (!m){
    shouldLoad = 1;
  } else{
    shouldLoad = 0;
  }

  if (!m && ((m = mmapAlloc()) == 0)){
    cprintf("Call a geographer: I'm out of maps!\n");
    return 0;
  }

  m->f = f;
  m->mapRef++;
  // m->f->ref++;
  // cprintf("ref: %d\n", m->f->ref);

  // Check if fd prot and map prot are compatible
  int fileProt = m->f->readable?1:0 | m->f->writable?2:0;
  if ( (fileProt & prot) == 0){
    return (void *) 0xffffffff;
  }

  // Extend the process size if necessary - you could check if such a mapping already exists in the process address space (not doing this currently)
  int newSz;
  char *start;

  newSz = PGROUNDUP(offset+length) - PGROUNDDOWN(offset);
  start = extend(currproc, currproc->sz, newSz);

  struct mmapInfo *vma = mmapAssign(m, 0, start, offset, length, prot, flags);
  offset = PGROUNDDOWN(offset);

  mmapAssignProc(currproc, vma);

  // Use the VMA

  // Allocate PTEs
  int status;
  if (( status = mmapAllocUvm(currproc->pgdir, vma, 0, 0)) <0 ){
    cprintf("Couldn't allocate\n");
    return 0;
  }

  // Load the pages
    // Use the struct legend and the vma to figure out which pages are needed and load only the necessary pages
  int written;

  if (offset >= f->ip->size || !shouldLoad){
    // Need to allocate pages still
    int pageIndex = vma->firstPageIndex;
    for (int i=0; i < vma->numPages; i++){
      vma->pages->ref[pageIndex]++;
    }
  } else{
    ilock(m->f->ip);
    if (( written = mmapLoadUvm(currproc->pgdir, m, vma, 0, 0)) < 0){
      cprintf("Loading failed\n");
      iunlock(m->f->ip);
      return 0;
    }
    iunlock(m->f->ip);
  }

  // printInfo(m);
  mmapdeallocuvm(currproc->pgdir, currproc->sz, currproc->sz - newSz);
  currproc->sz -= newSz;

  // freeVMA(vma);
  return m;
}

int writeToPageCache(struct legend2 *map, char *addr, int offset, int length){
  // Currently, cannot extend the file
  // Length doesn't exceed 512 * (MAXOPBLOCKS-1-1-2) = 512 * 3. That is guaranteed. So we would be only writing to atmost 2 pages
  // pagePointer points to the location we will be writing to. Written keeps track of the number of bytes written since the beginning. currentEndOfPage keeps track of the number of bytes to write to the current page
  char *pagePointer;
  int pageIndex, toWrite, canWrite;

  pageIndex = PGROUNDDOWN(offset) / PGSIZE;
  // fileLastPage = PGROUNDDOWN(map->f->ip->size) / PGSIZE;
  // lastPageBytes = (map->f->ip->size) % PGSIZE;
  toWrite = length;

  // How many maximum bytes can you write on the current page?
    // If offset = 0, write = 512 then can write = 4096 bytes
      // towrite = 512, so we can
    // If offset = 3584, write = 1024 then can write = 512 bytes
      // 512 are written and towrite becomes 512
      // Shift to the next page
      // offset = 0, write = 512, canwrite = 4096 bytes
      // 512 are written, towrite becomes 0
  offset = offset % PGSIZE;
  while (toWrite){
    pagePointer = map->physicalPages[pageIndex];
    if (!pagePointer){
      panic("871 Unmapped\n");
    }
    pagePointer += offset;

    canWrite = min(toWrite, PGSIZE - offset);
    memmove(pagePointer, addr, canWrite);

    toWrite = toWrite - canWrite;
    offset  = (offset + canWrite) % PGSIZE;

    addr = addr + canWrite;
    pageIndex++;
  }

  return length - toWrite;
}

int mmapAssignProc(struct proc *currproc, struct mmapInfo *vma){
  for (int i=0; i < NOMAPS; i++){
    if (!currproc->maps[i] || currproc->maps[i]->ref == 0){
      currproc->maps[i] = vma;
      return 0;
    }
  }
  return -1;
}

void *mmap_helper(void *addr, unsigned int length, int prot, int flags, int fd, int offset){
  // Anonymous Maps aren't cached, do you need to?
  // Process calls MAP_ANON and MAP_SHARED, you note this in the struct proc

  // Argument checking
  // Cannot be both SHARED and PRIVATE
  if (flags & MAP_SHARED & MAP_PRIVATE){
    return (void *) 0xffffffff;
  }

  if (fd == 0 && (flags & MAP_ANONYMOUS)){
  } else if (fd < 2 || fd >= NOFILE){
    return (void *) 0xffffffff;
  }

  // If you want to share MAP_ANON, then you only need to create mmapInfo and don't need to keep track of it

  // It can only be either MAP_SHARED | MAP_ANON or MAP_PRIVATE | MAP_ANON or only MAP_SHARED or MAP_PRIVATE
  // MAP_ANON just means that it isn't file backed and the only way to share it is with a child, or as a pointer?

  // ========================================SIGN THE AGREEMENT=============================================

  struct proc *currproc = myproc();
  struct legend2 *m = 0;
  struct anon *anonMap = 0;
  struct file *f = currproc->ofile[fd];

  if (!(flags & MAP_ANONYMOUS)){
    // Find in the global cache
    m = findInCache(f);

    if (!m && ((m = mmapAlloc()) == 0)){
      cprintf("Call a geographer: I'm out of maps!\n");
      return (void *) 0xffffffff;
    }

    m->f = currproc->ofile[fd];
    m->mapRef++;
    m->f->ref++;
    // cprintf("ref: %d\n", m->f->ref);

    // Check if fd prot and map prot are compatible
    int fileProt = m->f->readable?1:0 | m->f->writable?2:0;
    if ( (fileProt & prot) == 0){
      return (void *) 0xffffffff;
    }
  } else{
    // This is anonymous mapping
    // Get a struct anon from anonCache
    for (int i=0; i<NOMAPS; i++){
      if (anonCache.anonMaps->ref == 0){
        anonMap = &anonCache.anonMaps[i];
        break;
      }
    }

    if (!anonMap){
      return (void *) 0xffffffff;
    }

    anonMap->ref++;
    offset = 0;
  }

  // Extend the process size if necessary - you could check if such a mapping already exists in the process address space (not doing this currently)
  int newSz;
  char *start;

  newSz = PGROUNDUP(length);
  start = extend(currproc, currproc->sz, newSz);

  struct mmapInfo *vma = mmapAssign(m, anonMap, start, offset, length, prot, flags);
  offset = PGROUNDDOWN(offset);

  int status = 0;
  if ((status = mmapAssignProc(currproc, vma)) == -1){
    freeVMA(vma);
    panic("Out of VMAs");
    return MAP_FAILED;
  }

  return vma->start;
}

void clearAnon(struct anon *am){
  for (int i=0; i<3; i++){
    am->physicalPages[i] = 0;
  }
  am->ref = 0;
}

void clearMap(struct legend2 *m){
  m->f->ref--;
  m->f = 0;
  m->mapRef = 0;
  for (int i=0; i<MAX_PAGES; i++){
    m->physicalPages[i] = 0;
    m->ref[i] = 0;
  }
}

// Write to disk function
  // returns success or fail
int writeToDisk(struct mmapInfo *vma, int pageIndex){
  int n = PGSIZE;
  struct legend2 *m = vma->pages;
  char *addr = m->physicalPages[pageIndex];
  
  if (!m){
    panic("Should have had a legend\n");
  }

  if (pageIndex==vma->firstPageIndex+vma->numPages-1){
    // We're writing the last page, do not write beyond the vma specified
    if (((uint) vma->end % PGSIZE) == 0){
      // Skip
    } else{
      n = min((uint) vma->end - PGROUNDDOWN((uint) vma->end), PGSIZE);
    }
  }
  int r = 0;
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
  int i = 0;
  begin_op();
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;
    ilock(m->f->ip);
    if ((r = writei(m->f->ip, addr + i, m->f->off, n1)) > 0)
      m->f->off += r;
    iunlock(m->f->ip);
    if(r < 0)
      break;
    if(r != n1)
      panic("short filewrite");
    i += r;
  }
  end_op();

  return 0;
}

void clearPTE(pde_t *pgdir, char *va, int numPTEs){
  // Since VMA allocates consecutive PTEs, we just need to find the first PTE to clear and then we clear numPTEs
  pte_t *pte;

  pte = walkpgdir(pgdir, va, 0);
  if (!pte){
    panic("Yet to handle");
  }
  if (!(*pte & PTE_P)){
    panic("Yet to handle and understand");
  }

  for (int i=0; i < numPTEs; i++){
    *pte = 0;
    pte++;
  }
}

void freeMap(struct legend2 *map, int clearAll){
  // This should only happen when mapRef is 0
  if (map->mapRef != 0){
    panic("Freeing map that is referenced");
  }
  // We don't need to clear the PTEs. It will be cleared in freevm as well as the pages, but we need to check if the pages are dirty or not
  clearMap(map);
}

void freeAnon(struct anon *anon, int clearAll){
  // This should only happen when anon ref is 0
  if (anon->ref != 0){
    panic("Freeing map that is referenced");
  }
  // We don't need to clear the PTEs. It will be cleared in freevm as well as the pages, but we need to check if the pages are dirty or not
  clearAnon(anon);
}

void cleanUpVMA(struct mmapInfo *vma){
  struct proc *currproc = myproc();
  pte_t *pteIndex;
  char *addr = vma->start;
  if (vma->flags & (~MAP_ANONYMOUS)){
    if (vma->flags & MAP_SHARED){
      // cprintf("We are looking at a file-backed mapping which is shared\n");
      for (int i=0; i < MAX_PAGES; i++, addr+=PGSIZE){
        if (vma->pages && vma->pages->ref[i] > 0){
          vma->pages->ref[i]--;
          pteIndex = walkpgdir(currproc->pgdir, addr, 0);
          if (!pteIndex){
            panic("Yet to handled 1078");
          }
          if (!(*pteIndex & PTE_P)){
            continue;
          }
          getDetails(pteIndex);
          if (isDirty(pteIndex)){
            // cprintf("In cleanUpVMA: This page was dirty, writing it back to disk\n");
            writeToDisk(vma, i);
          }
          if (vma->pages->ref[i] == 0){
            // cprintf("In cleanupVMA: The ref count of this page was 0, hence, this page was freed\n");
            vma->pages->physicalPages[i] = 0;
          }
        }
      }
      vma->ref--;
      vma->pages->mapRef--;
      if (vma->pages->mapRef == 0){
        // cprintf("In cleanUpVMA: mapRef became 0 and so this map was freed\n");
        freeMap(vma->pages, 0);
      }
      if (vma->ref == 0){
        // cprintf("In cleanUpVMA: vma ref became 0 and so this vma was freed\n");
        freeVMA(vma);
      }
    } else{
      // cprintf("In cleanupVMA: We are looking at private file-backed mapping\n");
      vma->ref--;
      vma->pages->mapRef--;
      if (vma->ref == 0){
        // cprintf("cleanupVMA: The VMA wasn't shared by anyone, free the VMA and the vma->pages if the ref count is 0\n");
        if (vma->pages->mapRef == 0){
          // cprintf("cleanupVMA: Freeing the Map\n");
          freeMap(vma->pages, 0);
        }
        freeVMA(vma);
      } else{
        // cprintf("cleanupVMA: Clearing PTEs\n");
        clearPTE(currproc->pgdir, vma->start, vma->numPages);
      }
    }
  } else{
    // cprintf("We are looking at an anonymous mapping which is shared\n");
    vma->ref--;
    vma->anonMaps->ref--;
    if (vma->ref==0){
      if (vma->anonMaps->ref == 0){
        // cprintf("cleanupVMA: Anon ref 0, clearing anon\n");
        freeAnon(vma->anonMaps, 1);
      }
      freeVMA(vma);
    } else{
      // cprintf("cleanupVMA: Clearing PTEs\n");
      clearPTE(currproc->pgdir, vma->start, vma->numPages);
    }
  }
}

int munmap_helper(void *addr, unsigned int length){
  if (length < 0){
    return 0;
  }
  if (!addr){
    return 0;
  }

  char *roundAddr;
  struct mmapInfo *vma = 0;
  int vmaIndex = 0;

  roundAddr = (char *) PGROUNDDOWN((uint) addr);

  // Find the VMA
  struct proc *currproc = myproc();
  for (int i=0; i<NOMAPS; i++){
    if (currproc->maps[i] && currproc->maps[i]->ref){
      if (currproc->maps[i]->start <= roundAddr && roundAddr < currproc->maps[i]->end){
        vma = currproc->maps[i];
        vmaIndex = i;
        break;
      }
    }
  }

  // Possibly divide the VMA into 2
  // Does it span the entire VMA?
  struct mmapInfo *leftVMA = 0;
  struct mmapInfo *rightVMA = 0;
  if (roundAddr == vma->start && roundAddr + length >= vma->end){
  } else if (roundAddr == vma->start){
    // Divide into right-side VMA
    char *newStart = (char *) PGROUNDDOWN((uint) (vma->start + length));
    int newOffset = PGROUNDUP(vma->firstPageIndex * PGSIZE + length);
    rightVMA = mmapAssign(vma->pages, vma->anonMaps, newStart, newOffset, (uint) vma->end - (uint) newStart, vma->prot, vma->flags);
    if (!rightVMA){
      return -1;
    }
  } else if (roundAddr + length >= vma->end){
    // Divide into left-side VMA
    char *newStart = vma->start;
    int newOffset = (uint) roundAddr - (uint) vma->start;
    leftVMA = mmapAssign(vma->pages, vma->anonMaps, newStart, newOffset, newOffset, vma->prot, vma->flags);
    if (!leftVMA){
      return -1;
    }
  } else{
    // Divide into left-and-right-side VMA
    char *left = vma->start;
    char *leftEnd = roundAddr;
    int leftOffset = (uint) leftEnd - (uint) left;
    char *right = (char *) PGROUNDUP((uint) (roundAddr + length));
    char *rightEnd = vma->end;
    int rightOffset = (uint) rightEnd - (uint) right;
    leftVMA = mmapAssign(vma->pages, vma->anonMaps, left, leftOffset, leftOffset, vma->prot, vma->flags);
    rightVMA = mmapAssign(vma->pages, vma->anonMaps, right, rightOffset, rightOffset, vma->prot, vma->flags);
    if (!leftVMA || !rightVMA){
      if (leftVMA){
        freeVMA(leftVMA);
      } else if (rightVMA){
        freeVMA(rightVMA);
      }
      return -1;
    }
  }

  int status = 0;
  if (leftVMA){
    // Switch the VMAs in current process
    currproc->maps[vmaIndex] = leftVMA;

    // Now assign the rightVMA if it exists
    if (rightVMA){
      if ((status = mmapAssignProc(currproc, rightVMA)) == -1){
        return -1;
      }
    }
  } else if (rightVMA){
    currproc->maps[vmaIndex] = rightVMA;
  }

  if (!vma){
    return 0;
  }

  // So, we have to deallocate the memory of the process and possibly flush it back to disk
  char *start = roundAddr;
  char *end = (char *) min(PGROUNDUP((uint) (start + length)), PGROUNDUP((uint) vma->end));
  char *va = start;
  pte_t *pte;
  int pageIndex = (int) (roundAddr - vma->start) / PGSIZE;
  uint pa;

  struct legend2 *m;
  struct anon *am;
  int *ref;
  int *count = 0;

  m = vma->pages;
  am = vma->anonMaps;

  // For MAP_PRIVATE, we would like to discard the pages
  // For MAP_SHARED, check if the page's reference count has gone down or not and write to the disk
  if (m){
    ref = vma->pages->ref;
    for (; va < end; va += PGSIZE){
      if ((pte = walkpgdir(currproc->pgdir, va, 0)) == 0){
        panic("munmap: pte should exist");
      }
      if (!(*pte & PTE_P)){
        continue;
      }
      // Decrement the page reference count
      ref[pageIndex]--;
      if (ref[pageIndex] == 0){
        // Check if the page is dirty and write it back to the disk
        if (isDirty(pte)){
          writeToDisk(vma, pageIndex);
        }
        if(!pte)
          va = (char *) (PGADDR(PDX((uint) va) + 1, 0, 0) - PGSIZE);
        else if((*pte & PTE_P) != 0){
          pa = PTE_ADDR(*pte);
          if(pa == 0)
            panic("kfree");
          char *v = P2V(pa);
          kfree(v);
          *pte = 0;
        }
      }
      *pte = 0;
      pageIndex++;
      lcr3(V2P(currproc->pgdir));
    }
  } else{
    va = start;
    count = &vma->anonMaps->ref;
    for (; va < end; va += PGSIZE){
      if ((pte = walkpgdir(currproc->pgdir, va, 0)) == 0){
        panic("munmap: pte should exist");
      }
      // Decrement the page reference count
      (*count)--;
      if (*count == 0){
        clearAnon(am);

        if(!pte)
          va = (char *) (PGADDR(PDX((uint) va) + 1, 0, 0) - PGSIZE);
        else if((*pte & PTE_P) != 0){
          pa = PTE_ADDR(*pte);
          if(pa == 0)
            panic("kfree");
          char *v = P2V(pa);
          kfree(v);
          *pte = 0;
        }
      }
      // Remove the page table entry
      *pte = 0;
      pageIndex++;
    }
  }
  
  // Remove VMA from struct proc
  // Decrement the ref count. If the ref count is 0, free the VMA
  vma->ref--;
  if (vma->ref == 0){
    freeVMA(vma);
  } else{
    // Assign the leftVMA and rightVMA
  }
  return 0;
}

int handleMapFault(pde_t *pgdir, char *addr, struct mmapInfo *vma){
  pte_t *pte;
  if (( pte = walkpgdir(pgdir, (void *) addr, 0)) == 0){
    panic("Something's wrong");
  }
  if (*pte & PTE_P){
    if (*pte & PTE_W){
      panic("There shouldn't have been any fault\n");
    } else{
      if (vma->prot & PROT_WRITE){
        // 1. Alloc new pages over the same range
        // 2. Clear the initial page table entries with correct permissions
        vma->actualFlags |= PTE_W;
        mmapAllocUvm(pgdir, vma, 1, addr);
        vma->actualFlags &= ~PTE_W;
        // 3. Flush TLB: Reload the base register cr3
        // 4. Should I clear the map? Need to experiment with actual mmap You don't need to keep track of the private mappings
        return 0;
      }
    }
  } else{
    struct legend2 *m = vma->pages;
    // Use the VMA

    // Allocate PTEs
    int status;
    if (( status = mmapAllocUvm(pgdir, vma, 0, addr)) < 0 ){
      return -1;
    }

    if (!(vma->flags & MAP_ANONYMOUS)){
      // Load the pages
      int written;
      ilock(m->f->ip);
      if (( written = mmapLoadUvm(pgdir, m, vma, 0, addr)) < 0){
        iunlock(m->f->ip);
        return -1;
      }
      iunlock(m->f->ip);
      return 0;
    } else{
      return 0;
    }
  }
  return -1;
}

void clear(pde_t *pgdir, struct mmapInfo *m){
  // Get the pte, clear it
  pte_t *pte;

  pte = walkpgdir(pgdir, m->start, 0);
  for (int i=0; i<m->numPages; i++){
    if (!(*pte & PTE_P)){
      continue;
    }
    *pte = 0;
    pte++;
  }
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
