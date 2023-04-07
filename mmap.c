#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fd = open("README", O_RDWR);
    char *addr = (char *) mmap(0, 12, PROT_READ, MAP_SHARED, fd, 0);
    char *addr2 = (char *) mmap(0, 12, PROT_READ, MAP_SHARED, fd, 0);
    printf("%x %x %d\n", addr, addr2, getpid());
    sleep(100);
    return 0;
}
