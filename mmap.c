#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

int main(){
    int fd = open("README", O_RDWR);
    char *ret = mmap(0, 1, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ret[0] = 'c';
    ret[1] = 'd';
    munmap(ret, 4096);
    return 0;
}
