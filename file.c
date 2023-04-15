//
// File descriptors
//

#include "types.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "mmu.h"
#include "mman.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE)
    pipeclose(ff.pipe, ff.writable);
  else if(ff.type == FD_INODE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
int
filestat(struct file *f, struct stat *st)
{
  if(f->type == FD_INODE){
    ilock(f->ip);
    stati(f->ip, st);
    iunlock(f->ip);
    return 0;
  }
  return -1;
}

// Read from file f.
int
fileread(struct file *f, char *addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return piperead(f->pipe, addr, n);
  if(f->type == FD_INODE){
    if (f->ip!=(struct inode *) 0x80111a24){
      cprintf("Should read: %d %d %x %d\n", f->off, n, f->ip, f->ip->size);
      int prot = f->readable?PROT_READ:0 | f->writable?PROT_WRITE:0;
      struct legend2 *map = readIntoPageCache(0, n, prot, MAP_SHARED, f, f->off);
      r = readFromPageCache(map, addr, f->off, n);
      f->off += r;
    } else{
      // cprintf("Type: %d\n", f->type);
      ilock(f->ip);
      if((r = readi(f->ip, addr, f->off, n)) > 0)
        f->off += r;
      iunlock(f->ip);
    }
    // cprintf("Read bytes: %d\n", r);
    return r;
  }
  panic("fileread");
}

//PAGEBREAK!
// Write to file f.
int
filewrite(struct file *f, char *addr, int n)
{
  int r;

  if(f->writable == 0)
    return -1;
  if(f->type == FD_PIPE)
    return pipewrite(f->pipe, addr, n);
  if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * 512;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      if (f->ip != (struct inode *) 0x80111a24){
        // If not found in cache, readIntoPageCache and write to page cache
        // readIntoPageCache basically makes sure that the page will be in the cache
        // writeToPageCache makes sure that only the page cache is written to and nothing else
        cprintf("Should write: %d %d %x %d\n", f->off, n, f->ip, myproc()->pid);
        int prot = f->readable?PROT_READ:0 | f->writable?PROT_WRITE:0;
        struct legend2 *map = readIntoPageCache(0, n, prot, MAP_SHARED, f, f->off);
        // You should have gotten the appropriate pages into your page cache by now
        r = writeToPageCache(map, addr, f->off, n1);
        f->off += r;
        if(n > 0 && f->off > f->ip->size){
          f->ip->size = f->off;
          iupdate(f->ip);
        }
        cprintf("written\n");
        // cprintf("r: %d\n", r);
      } else{
        ilock(f->ip);
        if ((r = writei(f->ip, addr + i, f->off, n1)) > 0)
          f->off += r;
        iunlock(f->ip);
      }

      end_op();

      if(r < 0)
        break;
      if(r != n1)
        panic("short filewrite");
      i += r;
    }
    return i == n ? n : -1;
  }
  panic("filewrite");
}

