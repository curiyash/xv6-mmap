// Return 0xffffffff when mapping fails
#define MAP_FAILED ((void *)-1)

// Argument values for prot parameter
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4
#define PROT_NONE       0x0

// Argument values for flags parameter
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10

extern void *mmap(void *addr, unsigned int length, int prot, int flags, int fd, int offset);
extern int munmap(void *addr, unsigned int length);
