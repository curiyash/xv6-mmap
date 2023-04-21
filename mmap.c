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
    char *ret = mmap(0, 1, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    ret[0] = 'c';

    if (fork() == 0){
        sleep(2);
        printf("%c\n", ret[0]);
    } else{
        exit(0);
    }
    munmap(ret, 4096);
    return 0;
}
