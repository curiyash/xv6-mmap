#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fd = open("README", O_RDWR);

    char *addr = (char *) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // char *addr2 = (char *) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 8192);
    char *addr3 = (char *) mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 4096);
    char *addr2 = (char *) mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    printf("%p %p %p\n", addr, addr2, addr3);
    return 0;
}
