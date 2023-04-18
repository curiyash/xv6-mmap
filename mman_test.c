#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "mman.h"

#define n 1000

// Single process, MAP_SHARED - READ_ONLY
void smsro(){
    int fd = open("README", 0);

    char *ret = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 0);

    if (ret != (char *) 0xffffffff){
        if (ret[0] == 'x'){
        } else{
            printf(1, "19 Single process, MAP_SHARED, READ_ONLY fail\n");
            exit();
        }
    } else{
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }

    // if (munmap(ret, 4096) == -1){
    //     printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
    //     exit();
    // }
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void smswo(){
    int fd = open("README", O_RDWR);

    char *ret = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

     if (ret != (char *) 0xffffffff){
        // ret[0] = 'r';
        // printf(1, "ert: %c\n", ret[0]);
        if (ret[0] == 'x'){
        } else{
            printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
            exit();
        }
    } else{
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }

    // if (munmap(ret, 4096) == -1){
    //     printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
    //     exit();
    // }
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void leftVMAtest(){
    int fd = open("README", O_RDONLY);

    char *ret = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);

     if (ret != (char *) 0xffffffff){
        if (ret[0] == 'r'){
        } else{
            printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
            exit();
        }
    } else{
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }

    if (munmap(ret, 4096) == -1){
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }
    printf(1, "%c\n", ret[4096]);
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void rightVMAtest(){
    int fd = open("README", O_RDONLY);

    char *ret = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    printf(1, "-------------------------------------------\n");

    if (ret != (char *) 0xffffffff){
        if (ret[0] == 'r'){
        } else{
            printf(1, "1 Single process, MAP_SHARED, READ_ONLY fail\n");
            exit();
        }
    } else{
        printf(1, "2 Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }

    if (munmap(ret+4096, 4096) == -1){
        printf(1, "3 Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }
    printf(1, "%c\n", ret[0]);
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void sandwichTest(){
    int fd = open("README", O_RDONLY);

    char *ret = mmap(0, 4096 * 3, PROT_READ, MAP_SHARED, fd, 0);
    printf(1, "%c\n", ret[8194]);
    printf(1, "%c\n", ret[0]);

    if (ret != (char *) 0xffffffff){
        if (ret[0] == 'x'){
        } else{
            printf(1, "117 Single process, MAP_SHARED, READ_ONLY fail\n");
            exit();
        }
    } else{
        printf(1, "121 Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }

    // if (munmap(ret+4096, 4096) == -1){
    //     printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
    //     exit();
    // }
    printf(1, "%c\n", ret[0]);
    printf(1, "---\n");
    printf(1, "%c\n", ret[8194]);
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

int heloo(){
    unlink("heloo");
    int fd = open("heloo2", O_CREATE | O_RDWR);

    printf(1, "%d\n", fd);
    char buf[n];
    for (int i=0; i<n; i++){
        buf[i] = '1';
    }
    for (int i=0; i < 5; i++){
        printf(1, "%d ", i);
        write(fd, buf, n);
    }
    printf(1, "\n");
    printf(1, "Hello\n");
    close(fd);
    fd = open("heloo2", O_RDWR);

    int count = 0;
    for (int i=0; i < 5; i++){
        read(fd, buf, n);
        for (int i=0; i < n; i++){
            if (buf[i] == '1'){
                count++;
            }
        }
    }
    if (count == n * 5){
        printf(1, "mman_test ok");
    } else{
        printf(1, "oops %d\n", count);
        return 1;
    }
    close(fd);
    unlink("heloo2");
    return 0;
}

int main(){
    printf(1, "smsro\n");
    smsro();
    smswo();
    // leftVMAtest();
    // rightVMAtest();
    // sandwichTest();
    exit();
}
