#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fd = open("README", O_RDWR);

    char *addr = (char *) mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    // char *addr = (char *) mmap(NULL, 8, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (!addr){
        return 0;
    }
    // printf("%c\n", addr[0]);
    // addr[4] = 'N';
    // sleep(20);
    printf("%c\n", addr[100000]);
    // addr[10] = 'X';
    addr[100000] = 'D';
    printf("%c\n", addr[10]);
    return 0;
}
