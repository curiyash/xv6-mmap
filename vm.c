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
  struct spinlock lock;
  struct legend2 maps[NMAPS];
} mmapCache;

// Anonymous map cache
struct {
  struct spinlock lock;
  struct anon anonMaps[NMAPS];
} anonCache;

// mmapInfo structs
struct {
  struct spinlock lock;
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
    if (PTE_META(*pte) == (PTE_PRIVATE >> 8)){
      *pte &= ~PTE_W;
      *pte |= PTE_P;
      *pte &= ~PTE_PRIVATE;
    } else{
      *pte = pa | perm | PTE_P;
    }
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
            *pte |= PTE_SKIP;
            continue;
          }
          if (vma->flags & MAP_PRIVATE){
            *pte |= PTE_PRIVATE;
            *pte &= ~PTE_P;
          } else{
            *pte |= PTE_AVL;
            *pte &= ~PTE_P;
          }
        }
      }
    }
  }

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P)){
      if (PTE_META(*pte)==(PTE_AVL >> 8)){
        *pte |=  PTE_P;
        *pte &= ~PTE_AVL;
        continue;
      } else if (PTE_META(*pte)==(PTE_PRIVATE >> 8)){
        pte_t *newPte;
        if ((newPte = walkpgdir(d, (void *) i, 0)) == 0){
          panic("Copying MAP_PRIVATE went wrong");
        }
        *newPte = *pte;
        *pte |= PTE_P;
        *pte &= ~PTE_PRIVATE;
      } else if (*pte & PTE_SKIP){
        *pte &= ~PTE_SKIP;
        continue;
      }
      // panic("copyuvm: page not present");
      continue;
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

  acquire(&mmapCache.lock);
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
    mappingPagesFrom = ((uint) (faultedAddr - vma->start) / PGSIZE) + vma->firstPageIndex;
    a = PGROUNDDOWN((uint) faultedAddr);
    end = (char *) PGROUNDDOWN((uint) faultedAddr + PGSIZE);
    start = (char *) PGROUNDDOWN((uint) faultedAddr);
  }

  for(; a < (uint) end; a += PGSIZE){
    if (reload || !physicalPages[mappingPagesFrom]){
      if (!physicalPages[mappingPagesFrom] && am && (vma->prot & PROT_WRITE)){
        anonFlag = PTE_W;
      }
      mem = kalloc();
      memset(mem, 0, PGSIZE);
      if(mem == 0){
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, (uint) end, (uint) start);
        release(&mmapCache.lock);
        return -1;
      }
    }
    if (reload){
      pte_t *pte;
      if ( (pte = walkpgdir(pgdir, (void *) a, 0)) == 0){
        panic("Drunken walk\n");
      }
      memmove(mem, P2V(PTE_ADDR(*pte)), PGSIZE);
      *pte = 0;
    }
    if (!reload && !physicalPages[mappingPagesFrom]){
      physicalPages[mappingPagesFrom] = mem;
    } else if (!reload && physicalPages[mappingPagesFrom]){
      mem = physicalPages[mappingPagesFrom];
    }
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), vma->actualFlags | anonFlag) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, (uint) end, (uint) start);
      kfree(mem);
      release(&mmapCache.lock);
      return -1;
    }
    mappingPagesFrom++;
  }
  release(&mmapCache.lock);
  return 0;
}

struct legend2 *mmapAlloc(){
  acquire(&mmapCache.lock);
  for (int i=0; i<NMAPS; i++){
    if (mmapCache.maps[i].mapRef == 0){
      for (int j=0; j<MAX_PAGES; j++){
        mmapCache.maps[i].physicalPages[j] = 0;
      }
      release(&mmapCache.lock);
      return &mmapCache.maps[i];
    }
  }
  release(&mmapCache.lock);
  return 0;
}

struct mmapInfo *mmapAssign(struct legend2 *m, struct anon *am, char *start, int offset, int length, int prot, int flags, char fileOp){
  int numPages = (PGROUNDUP(offset + length) - PGROUNDDOWN(offset)) / PGSIZE;

  acquire(&mtable.lock);
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
  vma->fileOp = fileOp;

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

  if (vma->prot == PROT_NONE){
    vma->actualFlags = PTE_U;
  }
  release(&mtable.lock);

  return vma;
}

int min(int a, int b){
  return a<b?a:b;
}

int max(int a, int b){
  return a>b?a:b;
}

void printInfo(struct legend2 *m){
  for (int i=0; i<MAX_PAGES; i++){
    // cprintf("%x\n", m->physicalPages[i]);
  }
}

// Map the pages into struct legend2 only if they could be loaded
int mmapLoadUvm(pde_t *pgdir, struct legend2 *map, struct mmapInfo *m, int reload, char *faultedAddr){
  // pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz
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
    addr = (char *) PGROUNDDOWN((uint) faultedAddr);
    firstPageIndex = (int) (faultedAddr - m->start) / PGSIZE + m->firstPageIndex;
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
    pa = PTE_ADDR(*pte);
    // If the page was not in the cache
    if (!map->ref[pageNum]){
      if(sz - i < PGSIZE)
        n = sz - i;
      else
        n = PGSIZE;
      int written;
      begin_op();
      if(( written = readi(map->f->ip, P2V(pa), offset+i, n) ) != n){
        end_op();
        return -1;
      }
      end_op();
    }
    map->ref[pageNum]++;
    pageNum++;
  }
  return 0;
}

struct legend2 *findInCache(struct file *f){
  acquire(&mmapCache.lock);
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
  release(&mmapCache.lock);
  return m;
}

void freeVMA(struct mmapInfo *vma){
  int shouldRelease = 0;
  if (!holding(&mtable.lock)){
    acquire(&mtable.lock);
    shouldRelease = 1;
  }
  vma->start = 0;
  vma->end = 0;
  vma->firstPageIndex = -1;
  vma->flags = 0;
  vma->prot = 0;
  vma->numPages = 0;
  vma->pages = 0;
  vma->ref = 0;
  vma->anonMaps = 0;
  if (shouldRelease){
    release(&mtable.lock);
  }
}

int readFromPageCache(struct legend2 *m, char *addr, int offset, int length){
  int toRead, lastPage, canRead, readFrom, actualRead;
  char *page;

  if (offset > m->f->ip->size){
    return 0;
  }

  toRead = length;
  lastPage = m->f->ip->size / PGSIZE;
  if (m->f->ip->size % PGSIZE == 0){
    lastPage--;
  }
  actualRead = 0;
  // cprintf("Reading %d from %d\n", length, offset);

  while (toRead){
    int pageNum = (offset / PGSIZE);
    page = m->physicalPages[pageNum];
    readFrom = offset % PGSIZE;
    canRead = min(PGSIZE - readFrom, toRead);
    if (pageNum == lastPage){
      int bytes = (m->f->ip->size % PGSIZE);
      if (bytes == 0){
        canRead = min(toRead, PGSIZE);
      } else{
        canRead = min(bytes - readFrom, toRead);
      }
      // cprintf("-------------------------------%d %d %d %d-----------------------------------\n", lastPage, toRead, bytes, canRead);
    }
    // cprintf("From page %d at %d can read %d | File size: %d\n", pageNum, readFrom, canRead, m->f->ip->size);

    memmove(addr + actualRead, page + readFrom, canRead);

    toRead -= canRead;
    offset += canRead;
    actualRead += canRead;
    // Have you read the last page? If yes, break;
    if (pageNum == lastPage){
      break;
    }
  }

  if (length - toRead != actualRead){
    panic("Incorrect logic\n");
  }

  return actualRead;
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
      *pte = 0;
    }
  }
  return newsz;
}

struct mmapInfo *readIntoPageCache(void *addr, unsigned int length, int prot, int flags, struct file *f, int offset){
  struct proc *currproc = myproc();
  struct legend2 *m = 0;
  // cprintf("Reading into Page Cache\n");

  // Find in the global cache
  m = findInCache(f);

  if (!m && ((m = mmapAlloc()) == 0)){
    cprintf("Call a geographer: I'm out of maps!\n");
    return 0;
  }

  acquire(&mmapCache.lock);
  m->f = f;
  m->mapRef++;
  m->f->ref++;
  release(&mmapCache.lock);

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

  struct mmapInfo *vma = mmapAssign(m, 0, start, offset, length, prot, flags, '1');
  offset = PGROUNDDOWN(offset);

  int status = 0;
  if ((status = mmapAssignProc(currproc, vma)) == -1){
    return 0;
  }

  // Use the VMA

  // Allocate PTEs
  if (( status = mmapAllocUvm(currproc->pgdir, vma, 0, 0)) <0 ){
    cprintf("Couldn't allocate\n");
    return 0;
  }

  // Load the pages
    // Use the struct legend and the vma to figure out which pages are needed and load only the necessary pages
  int written;

  if (offset >= f->ip->size){
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
  return vma;
}

int writeToPageCache(struct legend2 *map, char *addr, int offset, int length){
  // This needs to update the VMA end
  char *pagePointer;
  int pageIndex, toWrite, canWrite;

  pageIndex = PGROUNDDOWN(offset) / PGSIZE;
  toWrite = length;

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

struct legend2 *findInCachePage(struct file *f, int offset, int length, int op){
  struct legend2 *map = findInCache(f);
  if (!map && ((map = mmapAlloc())) == 0){
    panic("Out of maps\n");
    return 0;
  }
  if (!op && offset >= f->ip->size){
    return 0;
  }
  int pageNum = offset / PGSIZE;
  int lastPage = (offset + length) / PGSIZE;
  if ((offset + length) % PGSIZE == 0){
    lastPage--;
  }
  int prot = f->readable?PROT_READ:0 | f->writable?PROT_WRITE:0;
  for (int page = pageNum; page <= lastPage; page++){
    // Check if page is present
    if (map->physicalPages[page]){
    } else{
      // If not, read into the cache
      readIntoPageCache(0, PGSIZE, prot, MAP_SHARED, f, page * PGSIZE);
    }
  }
  return map;
}

void *mmap_helper(void *addr, int length, int prot, int flags, int fd, int offset){
  // Anonymous Maps aren't cached, do you need to?
  // Process calls MAP_ANON and MAP_SHARED, you note this in the struct proc

  if (length <= 0){
    return MAP_FAILED;
  }

  if (PGROUNDDOWN(offset) != offset){
    return MAP_FAILED;
  }

  struct proc *currproc = myproc();
  struct legend2 *m = 0;
  struct anon *anonMap = 0;
  struct file *f = 0;

  switch(flags){
    case MAP_SHARED: 
    case MAP_PRIVATE:
      if (fd < 0 || fd > NOFILE){
        return MAP_FAILED;
      }
      f = currproc->ofile[fd];
      if (f->ip == (struct inode *) 0x80112a24){
        return MAP_FAILED;
      }
      break;
    case MAP_ANONYMOUS | MAP_PRIVATE: 
    case MAP_ANONYMOUS | MAP_SHARED: 
      offset = 0;
      break;
    default: return MAP_FAILED;
  }

  if (!(flags & MAP_ANONYMOUS)){
    // Find in the global cache
    m = findInCache(f);

    if (!m && ((m = mmapAlloc()) == 0)){
      cprintf("Call a geographer: I'm out of maps!\n");
      return (void *) 0xffffffff;
    }

    acquire(&mmapCache.lock);
    m->f = currproc->ofile[fd];
    m->mapRef++;
    m->f->ref++;
    release(&mmapCache.lock);

    // Check if fd prot and map prot are compatible
    int fileProt = (m->f->readable?1:0) | (m->f->writable?2:0);
    if ( (fileProt & prot) != prot){
      if (prot == PROT_NONE){
      } else{
        return (void *) 0xffffffff;
      }
    }
  } else{
    // This is anonymous mapping
    // Get a struct anon from anonCache
    acquire(&anonCache.lock);
    for (int i=0; i<NMAPS; i++){
      if (anonCache.anonMaps[i].ref == 0){
        anonMap = &anonCache.anonMaps[i];
        break;
      }
    }

    if (!anonMap){
      release(&anonCache.lock);
      return (void *) 0xffffffff;
    }

    anonMap->ref++;
    release(&anonCache.lock);
    offset = 0;
  }

  // Extend the process size if necessary - you could check if such a mapping already exists in the process address space (not doing this currently)
  int newSz;
  char *start;

  newSz = PGROUNDUP(length);
  start = extend(currproc, currproc->sz, newSz);

  struct mmapInfo *vma = mmapAssign(m, anonMap, start, offset, length, prot, flags, '\0');
  offset = PGROUNDDOWN(offset);

  int status = 0;
  if ((status = mmapAssignProc(currproc, vma)) == -1){
    freeVMA(vma);
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
  acquire(&mmapCache.lock);
  int n = PGSIZE;
  struct legend2 *m = vma->pages;
  char *addr = m->physicalPages[pageIndex];
  
  if (!m){
    panic("Should have had a legend\n");
  }

  m->f->off = pageIndex * PGSIZE;

  if (pageIndex==vma->firstPageIndex+vma->numPages-1){
    // We're writing the last page, do not write beyond the vma specified
    if (((uint) vma->end % PGSIZE) == 0){
      // Skip
    } else{
      n = min((uint) vma->end - PGROUNDDOWN((uint) vma->end), PGSIZE);
      // int checkForHole = 0;
      // int lastByteWritten = 0;
      // int hole = 1;
      // // Or till the actual number of bytes written. Was there an intentions to create a hole?
      // for (int i=0; i<PGSIZE; i++){
      //   if (addr[i]=='\0'){
      //     lastByteWritten = i;
      //     checkForHole = 1;
      //   }
      //   if (checkForHole && addr[i]!='\0'){
      //     hole = 1;
      //   }
      // }
    }
  }
  int r = 0;
  int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
  int i = 0;
  while(i < n){
    int n1 = n - i;
    if(n1 > max)
      n1 = max;
    release(&mmapCache.lock);
    begin_op();
    ilock(m->f->ip);
    if ((r = writei(m->f->ip, addr + i, m->f->off, n1)) > 0)
      m->f->off += r;
    iunlock(m->f->ip);
    end_op();
    acquire(&mmapCache.lock);
    if(r < 0){
      break;
    }
    if(r != n1)
      panic("short filewrite");
    i += r;
  }

  release(&mmapCache.lock);
  return 0;
}

void clearPTE(pde_t *pgdir, char *va, int numPTEs){
  // Since VMA allocates consecutive PTEs, we just need to find the first PTE to clear and then we clear numPTEs
  pte_t *pte;

  pte = walkpgdir(pgdir, va, 0);
  if (!pte){
    panic("Yet to handle");
  }

  for (int i=0; i < numPTEs; i++){
    if (!(*pte & PTE_P)){
      pte++;
      continue;
    }
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

void printVMAStatistics(struct mmapInfo *vma){
  cprintf("Ref: %d | %d | %d | %c %d\n", vma->ref, vma->firstPageIndex, vma->numPages, vma->fileOp, vma->fileOp=='1'?vma->fileOp:0);
  if (vma->pages){
    for (int i=vma->firstPageIndex; i < vma->firstPageIndex + vma->numPages; i++){
      cprintf("---%d ", vma->pages->ref[i]);
    }
  }
  cprintf("\n");
}

void setDirty(pte_t *pte, int bit){
  *pte &= ~PTE_DIRTY;
}

void cleanUpVMA(struct mmapInfo *vma){
  struct proc *currproc = myproc();
  pte_t *pteIndex;
  char *addr = vma->start;
  acquire(&mtable.lock);
  if ((vma->flags & (MAP_ANONYMOUS)) == 0){
    if (vma->flags & MAP_SHARED){
      // int update = 0;
      // if (vma->pages->f->off >= vma->pages->f->ip->size){
      //   update = 1;
      // }
      // Use MAXPAGES if any process can write other processes' changes to the file back to disk
      // printVMAStatistics(vma);
      for (int i=vma->firstPageIndex; i < vma->firstPageIndex + vma->numPages; i++, addr+=PGSIZE){
        if (vma->pages && (uint) vma->pages->physicalPages[i] > 0){
          vma->pages->ref[i]--;
          pteIndex = walkpgdir(currproc->pgdir, addr, 0);
          if (!pteIndex){
            panic("Yet to handled 1078");
          }
          if (!(*pteIndex & PTE_P) && !vma->fileOp){
            continue;
          }
          if (isDirty(pteIndex) || vma->fileOp){
            release(&mtable.lock);
            writeToDisk(vma, i);
            acquire(&mtable.lock);
            setDirty(pteIndex, 0);
          }
          if (vma->pages->ref[i] == 0){
            vma->pages->physicalPages[i] = 0;
          } else{
            clearPTE(currproc->pgdir, addr, 1);
          }
        }
      }
      vma->ref--;
      vma->pages->mapRef--;
      if (vma->pages->mapRef == 0){
        freeMap(vma->pages, 0);
      }
      if (vma->ref == 0){
        freeVMA(vma);
      }
    } else{
      vma->ref--;
      vma->pages->mapRef--;
      if (vma->ref == 0){
        if (vma->pages->mapRef == 0){
          freeMap(vma->pages, 0);
        }
        freeVMA(vma);
      } else{
        clearPTE(currproc->pgdir, vma->start, vma->numPages);
      }
    }
  } else{
    vma->ref--;
    vma->anonMaps->ref--;
    if (vma->ref==0){
      if (vma->anonMaps->ref == 0){
        freeAnon(vma->anonMaps, 1);
      }
      freeVMA(vma);
    } else{
      clearPTE(currproc->pgdir, vma->start, vma->numPages);
    }
  }
  release(&mtable.lock);
}

int munmap_helper(void *addr, unsigned int length){
  if (length <= 0){
    return -1;
  }
  if (!addr || (PGROUNDDOWN((uint) addr) != (uint) addr )){
    return -1;
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

  if (!vma){
    return 0;
  }

  // Possibly divide the VMA into 2
  // Does it span the entire VMA?
  struct mmapInfo *leftVMA = 0;
  struct mmapInfo *rightVMA = 0;
  if (roundAddr == vma->start && roundAddr + length >= vma->end){
    // Entire VMA
  } else if (roundAddr == vma->start){
    // Divide into right-side VMA
    char *newStart = (char *) PGROUNDDOWN((uint) (vma->start + length));
    int newOffset = PGROUNDUP(vma->firstPageIndex * PGSIZE + length);
    rightVMA = mmapAssign(vma->pages, vma->anonMaps, newStart, newOffset, (uint) vma->end - (uint) newStart, vma->prot, vma->flags, '\0');
    if (!rightVMA){
      return -1;
    }
  } else if (roundAddr + length >= vma->end){
    // Divide into left-side VMA
    char *newStart = vma->start;
    int newOffset = (uint) roundAddr - (uint) vma->start;
    leftVMA = mmapAssign(vma->pages, vma->anonMaps, newStart, newOffset, newOffset, vma->prot, vma->flags, '\0');
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
    leftVMA = mmapAssign(vma->pages, vma->anonMaps, left, leftOffset, leftOffset, vma->prot, vma->flags, '\0');
    rightVMA = mmapAssign(vma->pages, vma->anonMaps, right, rightOffset, rightOffset, vma->prot, vma->flags, '\0');
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

  // So, we have to deallocate the memory of the process and possibly flush it back to disk
  char *start = roundAddr;
  char *end = (char *) min(PGROUNDUP((uint) (start + length)), PGROUNDUP((uint) vma->end));
  char *va = start;
  pte_t *pte;
  int pageIndex = (int) (roundAddr - vma->start) / PGSIZE + vma->firstPageIndex;
  // cprintf("------------------%d %d--------------------\n", (int) (roundAddr - vma->start) / PGSIZE, vma->firstPageIndex);
  // cprintf("---------------------------------%d-----------------------------------\n", vma->pages->f->ip->size);
  if (vma->fileOp){
    vma->end = vma->start + vma->pages->f->ip->size;
  }
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
        // Check if the page is dirty and write it back to the disks
        if (isDirty(pte)){
          writeToDisk(vma, pageIndex);
        }
        m->physicalPages[pageIndex] = 0;
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
      // THIS IS DOUBTFUL
      *pte = 0;
      pageIndex++;
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
  lcr3(V2P(currproc->pgdir));
  return 0;
}

void handleMapFault(char *addr){
  struct mmapInfo *vma = 0;
  struct proc *currproc = myproc();

  acquire(&mtable.lock);
  for (int i=0; i<NOMAPS; i++){
    struct mmapInfo *map = currproc->maps[i];
    if (map && map->start <= addr && addr < map->end){
      vma = map;
      break;
    }
  }
  release(&mtable.lock);

  pte_t *pte;
  if (( pte = walkpgdir(currproc->pgdir, (void *) addr, 0)) == 0){
    panic("Something's wrong");
  }
  if (!vma){
    // We shouldn't be handling this
    cprintf("Paging fault 1\n");
    exit();
    return;
  }

  if (*pte & PTE_P){
    if (*pte & PTE_W){
      panic("There shouldn't have been any fault\n");
    } else{
      if (vma->prot & PROT_WRITE){
        // This would only happen for MAP_PRIVATE maps
        // 1. Alloc new pages over the same range
        // 2. Clear the initial page table entries with correct permissions
        acquire(&mtable.lock);
        vma->actualFlags |= PTE_W;
        mmapAllocUvm(currproc->pgdir, vma, 1, addr);
        vma->actualFlags &= ~PTE_W;
        // 3. Flush TLB: Reload the base register cr3
        // 4. Should I clear the map? Need to experiment with actual mmap You don't need to keep track of the private mappings
        release(&mtable.lock);
        return;
      } else{
        cprintf("Paging fault 2\n");
        exit();
        return;
      }
    }
  } else{
    if (vma->prot == PROT_NONE){
      cprintf("Paging fault 3\n");
      exit();
      return;
    }
    struct legend2 *m = vma->pages;
    int status;
    if (( status = mmapAllocUvm(currproc->pgdir, vma, 0, addr)) < 0 ){
      return;
    }

    if (!(vma->flags & MAP_ANONYMOUS)){
      // Load the pages
      int written;
      ilock(m->f->ip);
      if (( written = mmapLoadUvm(currproc->pgdir, m, vma, 0, addr)) < 0){
        iunlock(m->f->ip);
        return;
      }
      iunlock(m->f->ip);
      return;
    } else{
      return;
    }
  }
  return;
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
