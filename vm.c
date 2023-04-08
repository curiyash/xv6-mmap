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

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

struct {
  // struct spinlock lock;
  struct legend2 maps[NMAPS];
} mmapCache;

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
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    // cprintf("Bad 1\n");
    if((mem = kalloc()) == 0)
      goto bad;
    // cprintf("Bad 2\n");
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
    // cprintf("Came till mappages just fine\n");
    cprintf("Copyuvm address: %x\n", P2V(PTE_ADDR(*pte)));
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
  // cprintf("newsz: %d\n", newsz);
  p->sz += newsz;
  return (char *) oldsz;
}

// VMA carries the entire information of how we want to map the pages
int mmapAllocUvm(pde_t *pgdir, struct legend2 *m, struct mmapInfo *vma){
  char *mem;
  uint a;
  int mappingPagesFrom;

  mappingPagesFrom = vma->firstPageIndex;
  // cprintf("first page index: %d\n", vma->firstPageIndex);

  a = (uint) vma->start;

  for(; a < (uint) vma->end; a += PGSIZE){
    if (!m->physicalPages[mappingPagesFrom]){
      mem = kalloc();
      if(mem == 0){
        cprintf("allocuvm out of memory\n");
        deallocuvm(pgdir, (uint) vma->end, (uint) vma->start);
        return -1;
      }
      memset(mem, 0, PGSIZE);
      m->physicalPages[mappingPagesFrom] = mem;
      // cprintf("1: %x\n", mem);
    } else{
      mem = m->physicalPages[mappingPagesFrom];
      // cprintf("2: %x\n", mem);
    }
    cprintf("Allocating page %x for %x\n", mem, a);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, (uint) vma->end, (uint) vma->start);
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

struct mmapInfo *mmapAssign(struct legend2 *m, char *start, int offset, int length, int prot, int flags){
  int numPages = (PGROUNDUP(offset + length) - offset) / PGSIZE;
  // cprintf("Number of pages: %d\n", numPages);

  // Allocate a struct mmapInfo
  struct mmapInfo *mI = 0;
  for (int i=0; i<NMAPS; i++){
    if (mtable.info[i].valid==0){
      mI = &mtable.info[i];
      break;
    }
  }

  mI->start = start;
  mI->end = start + length;
  mI->pages = m;
  mI->valid = 1;
  mI->firstPageIndex = offset / PGSIZE;
  mI->numPages = numPages;
  mI->prot = prot;
  mI->flags = flags;

  return mI;
}

int min(int a, int b){
  return a<b?a:b;
}

char *getPTE(char *start){
  return (char *) PTE_ADDR(walkpgdir(myproc()->pgdir, start, 0));
}

void printInfo(struct legend2 *m){
  for (int i=0; i<MAX_PAGES; i++){
    // cprintf("%d: %x\n", i, m->physicalPages[i]);
  }
}

// Map the pages into struct legend2 only if they could be loaded
int mmapLoadUvm(pde_t *pgdir, struct legend2 *map, struct mmapInfo *m){
  // pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz
  // cprintf("Offset: %d | i: %d | offset + i: %d\n", offset, i, offset + i);a
  uint i, pa, n, sz, offset;
  pte_t *pte;
  int pageNum;
  char *addr;

  addr = m->start;
  sz = min(m->numPages*PGSIZE, map->f->ip->size - m->firstPageIndex*PGSIZE);
  offset = m->firstPageIndex * PGSIZE;
  pageNum = m->firstPageIndex;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    cprintf("Loaduvm address: %x\n", P2V(PTE_ADDR(*pte)));
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
  vma->valid = 0;
}

int readFromPageCache(struct legend2 *m, char *addr, int offset, int length){
  if (!addr){
    return 0;
  }
  if (offset >= m->f->ip->size){
    return 0;
  }
  int fromPage = PGROUNDDOWN(offset) / PGSIZE;
  int lastPage = PGROUNDDOWN(offset + length) / PGSIZE;

  if ( (offset + length) % PGSIZE == 0){
    lastPage--;
  }

  // cprintf("fromPage: %d | lastPage: %d | %d %d\n", fromPage, lastPage, offset, length);

  if (!m->physicalPages[fromPage]){
      panic("Page was not allocated\n");
  }

  char *copyFrom = m->physicalPages[fromPage] + (offset - PGROUNDDOWN(offset));
  int copy = length;
  // cprintf("%d %d %d\n", length, PGSIZE - offset, m->f->ip->size);
  // cprintf("Copying from: %x %d bytes\n", copyFrom, copy);

  memmove(addr, copyFrom, copy);
  if (addr[0] == '\0'){
    return 0;
  }

  uint a = copy;

  for (int i = fromPage+1; i < lastPage; i++){
    if (!m->physicalPages[i]){
      panic("Page was not allocated\n");
    }
    memmove(addr + a, m->physicalPages[i], PGSIZE);
    if (addr[a] == '\0'){
      return 0;
    }
    a += PGSIZE;
  }

  if (lastPage!=fromPage){
    int rem = length - PGROUNDDOWN(offset+length);
    // cprintf("rem: %d\n", rem);

    if (!m->physicalPages[lastPage]){
        panic("Page was not allocated\n");
    }
    memmove(addr+a, m->physicalPages[lastPage], rem);

    // cprintf("Done copying: %d\n", length);

    if (addr[0] == '\0'){
      return 0;
    }
  }
  return length;
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
  offset = PGROUNDDOWN(offset);

  // Find in the global cache
  m = findInCache(f);

  if (!m && ((m = mmapAlloc()) == 0)){
    cprintf("Call a geographer: I'm out of maps!\n");
    return 0;
  }

  m->f = f;
  m->mapRef++;
  m->mapRef++;
  m->f->ref++;
  // cprintf("ref: %d\n", m->f->ref);

  // Check if fd prot and map prot are compatible
  int fileProt = m->f->readable?1:0 | m->f->writable?2:0;
  if ( (fileProt & prot) == 0){
    return (void *) 0xffffffff;
  }

  // Extend the process size if necessary - you could check if such a mapping already exists in the process address space (not doing this currently)
  int newSz;
  char *start;

  newSz = PGROUNDUP(length);
  start = extend(currproc, currproc->sz, newSz);

  struct mmapInfo *vma = mmapAssign(m, start, offset, length, prot, flags);

  // Use the VMA

  // Allocate PTEs
  int status;
  if (( status = mmapAllocUvm(currproc->pgdir, m, vma)) <0 ){
    cprintf("Couldn't allocate\n");
    return 0;
  }

  // Load the pages
    // Use the struct legend and the vma to figure out which pages are needed and load only the necessary pages
  int written;
  cprintf("Loading...\n");

  ilock(m->f->ip);
  if (( written = mmapLoadUvm(currproc->pgdir, m, vma)) < 0){
    // cprintf("Loading failed\n");
    iunlock(m->f->ip);
    return 0;
  }
  iunlock(m->f->ip);

  printInfo(m);
  mmapdeallocuvm(currproc->pgdir, currproc->sz, currproc->sz - newSz);
  currproc->sz -= newSz;

  freeVMA(vma);
  return m;
}

void *mmap_helper(void *addr, unsigned int length, int prot, int flags, int fd, int offset){
  struct proc *currproc = myproc();
  struct legend2 *m = 0;
  offset = PGROUNDDOWN(offset);

  struct file *f = currproc->ofile[fd];

  // Find in the global cache
  m = findInCache(f);

  if (!m && ((m = mmapAlloc()) == 0)){
    cprintf("Call a geographer: I'm out of maps!\n");
    return (void *) 0xffffffff;
  }

  m->f = currproc->ofile[fd];
  m->mapRef++;
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

  newSz = PGROUNDUP(length);
  start = extend(currproc, currproc->sz, newSz);

  struct mmapInfo *vma = mmapAssign(m, start, offset, length, prot, flags);

  // Assign to the process
  for (int i=0; i<NOMAPS; i++){
    // cprintf("%x\n", currproc->maps[i]);
    if (currproc->maps[i]==0){
      // cprintf("Assigning: %d %d\n", i, currproc->pid);
      currproc->maps[i] = vma;
      break;
    } else{
      // cprintf("%x %x %x %d %d\n", vma->start, vma->end, vma->pages, vma->numPages, vma->valid);
    }
  }

  // Use the VMA

  // Allocate PTEs
  int status;
  if (( status = mmapAllocUvm(currproc->pgdir, m, vma)) <0 ){
    cprintf("Couldn't allocate\n");
    return (void *) 0xffffffff;
  }

  // Load the pages
    // Use the struct legend and the vma to figure out which pages are needed and load only the necessary pages
  int written;

  // cprintf("Loading...\n");

  ilock(m->f->ip);
  if (( written = mmapLoadUvm(currproc->pgdir, m, vma)) < 0){
    cprintf("Loading failed\n");
    iunlock(m->f->ip);
    return (void *) 0xffffffff;
  }
  iunlock(m->f->ip);

  printInfo(m);
  return vma->start;
}

int munmap_helper(void *addr, unsigned int length){
  // cprintf("munmap: %d\n", (int) addr);
  struct proc *currproc = myproc();

  // Round down the address
  char *roundAddr = (char *) PGROUNDDOWN((unsigned int) addr);

  // Find which vma the address belongs to
  struct mmapInfo *m = 0;
  for (int i=0; i<NOMAPS; i++){
    if (currproc->maps[i]->valid && currproc->maps[i]->start<=roundAddr && roundAddr<currproc->maps[i]->end){
      m = currproc->maps[i];
      break;
    }
  }

  if (m==0){
    // cprintf("Found none\n");
    return 0;
  }

  char *va = roundAddr;
  char *end = (char *) min((int) m->end, (int) ((char *) addr+PGROUNDUP(length)));

  char *firstPage = roundAddr;
  // char *lastPage = (char *) PGROUNDUP((int) (addr + length));
  // int numPages = (lastPage - firstPage) / PGSIZE;
  int startingPage = (firstPage - m->start) / PGSIZE;
  // cprintf("Starting page: %d\n", startingPage);

  // cprintf("Clearing %d pages\n", numPages);

  pte_t *pte;
  // cprintf("Clearing: %d %d\n", (int) va, (int) end);
  for (; va<end; va+=PGSIZE){
    if ( (pte = walkpgdir(currproc->pgdir, (void *) va, 0)) == 0){
      panic("Couldn't get PTE");
    }
    if (!(*pte & PTE_P)){
      panic("Should've been mapped");
    }

    // If dirty, write back to disk
    // getDetails(pte);
    if (isDirty(pte)){
      cprintf("Page was dirty\n");
      if (filewrite(m->pages->f, (char *) va, PGSIZE) < 0){
        cprintf("something wrong\n");
      }
    }

    uint pa = PTE_ADDR(*pte);
    // cprintf("Before\n");
    if (pa==0){
      panic("kfree");
    }
    // cprintf("After\n");
    char *v = P2V(pa);
    kfree(v);
    *pte = 0;

    // Decrement the reference count for the page
    // cprintf("Reference count for page %d was: %d\n", startingPage, m->pages->ref[startingPage]);
    m->pages->ref[startingPage]--;
    // Decrement the reference count for the map
    // m->pages->mapRef--;
    // Remove the physical page
    m->pages->physicalPages[startingPage++] = 0;
  }

  printInfo(m->pages);

  cprintf("munmap helper\n");
  return 0;
}

void clear(pde_t *pgdir, struct mmapInfo *m){
  // Get the pte, clear it
  pte_t *pte;

  pte = walkpgdir(pgdir, m->start, 0);
  if (!pte){
    panic("Expected to be mapped\n");
  }
  for (int i=0; i<m->numPages; i++){
    cprintf("Exit Address: %x %x\n", m->start, P2V(PTE_ADDR(*pte)));
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

