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
    // Create a MAP_PRIVATE map
    // Change something
    // Is it visible to the child? Yes
    // Is change in child visible to the parent
    char *ret = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    printf("char: %c\n", ret[0]);
    ret[0] = 'y';
    printf("char: %c\n", ret[0]);
    if (fork() == 0){
        // Child
        printf("In child, char: %c\n", ret[0]);
        printf("In child, char: %c\n", ret[1]);
        ret[1] = 'z';
        printf("In child, char: %c\n", ret[1]);
    } else{
        wait(0);
        printf("In parent, char: %c\n", ret[0]);
        printf("In parent, char: %c\n", ret[1]);
    }
    return 0;
}
