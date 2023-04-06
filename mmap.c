#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(){
    int fd = open("README", O_RDWR);
    char c = 'x';

    printf("Reading... %d\n", getpid());

    sleep(10);

    // read(fd, &c, 1);
    write(fd, &c, 1);

    printf("Read\n");

    sleep(40);

    printf("Mapping\n");

    char *addr = (char *) mmap(NULL, 24, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    printf("addr: %x\n", addr);
    printf("addr[0]: %c\n", addr[0]);

    printf("Done\n");

    sleep(100);
    return 0;
}
