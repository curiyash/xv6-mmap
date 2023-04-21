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

    char *ret = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);

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

    if (munmap(ret, 4096) == -1){
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void smswo(){
    int fd = open("README", O_RDWR);

    char *ret = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 4096);

     if (ret != (char *) 0xffffffff){
        ret[0] = 'r';
        if (ret[0]=='r'){
        } else{
            printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
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

void smsno(){
    // No access to the mapped page
    // PROT_NONE and MAP_SHARED doesn't make sense. You allocate the page in pte, but don't load anything. There is no region of memory. It's just inaccessible
    int fd = open("README", O_RDWR);

    char *ret = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 0);
    char *ret2 = mmap(0, 4096, PROT_NONE, MAP_SHARED, fd, 4096);
    char *ret3 = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 8192);


     if (ret != MAP_FAILED && ret2 != MAP_FAILED && ret3 !=MAP_FAILED){
        // Should pagefault
        printf(1, "0th char: %c | 8192th char: %c\n", ret[0], ret3[0]);
        printf(1, "This should page fault and never return\n");
        printf(1, "%c\n", ret2[0]);
    } else{
        printf(1, "mmap fail\n");
        exit();
    }

    if (munmap(ret, 4096) == -1 && munmap(ret2, 4096) == -1 && munmap(ret3, 4096) == -1){
        printf(1, "munmap fail\n");
        exit();
    }
    printf(1, "PROT_NONE check fail\n");
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

void sanity_check(){
    int fd = open("README", 0);

    char c[512];
    for (int i=0; i<3; i++){
        for (int j=0; j<8; j++){
            read(fd, c, 512);
            if (j==0){
                printf(1, "%c\n", c[0]);
            }
        }
    }
    // read(fd, buf, 4096);
    // read(fd, buf, 4096);
    // printf(1, "%c\n", buf[0]);
}

void smp(){
    // Single process MAP_PRIVATE testing
    // Read test
    // Write test
    // None test
    int fd = open("README", O_RDWR);
    char *read_map = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    char *write_map = mmap(0, 4096, PROT_WRITE, MAP_PRIVATE, fd, 4096);
    char *none_map = mmap(0, 4096, PROT_NONE, MAP_PRIVATE, fd, 8192);

    if (read_map==MAP_FAILED || write_map==MAP_FAILED || none_map==MAP_FAILED){
        printf(1, "mmap fail %x %x %x\n", read_map, write_map, none_map);
        return;
    }

    if (read_map[0]=='x'){

    } else{
        printf(1, "MAP_PRIVATE PROT_READ error\n");
    }

    char before = write_map[0];
    printf(1, "Before writing: %c\n", before);
    // This should trigger a copy-on-write => Another pagefault
    write_map[0] = 'x';
    printf(1, "After writing: %c\n", write_map[0]);
    if (write_map[0]=='x'){

    } else{
        printf(1, "MAP_PRIVATE PROT_WRITE error\n");
    }
    char c[512];
    for (int i=0; i<1; i++){
        for (int j=0; j<8; j++){
            read(fd, c, 512);
        }
    }
    read(fd, c, 512);
    if (c[0] != before){
        printf(1, "MAP_PRIVATE copy-on-write error\n");
    }

    // This should page fault and not return
    if (none_map[0]){
        printf(1, "MAP_PRIVATE None failed\n");
        return;
    }

    if (munmap(read_map, 4096) == -1 || munmap(read_map, 4096) == -1 || munmap(read_map, 4096) == -1 ){
        printf(1, "Unmapping fail\n");
        return;
    }
}

// MAP_SHARED tests with fork
// Producer-consumer kind of code
// 1st process writes to the file
// 2nd process reads from the file
// Check if both are same
void msf(){
    int fd = open("README", O_RDWR);
    if (fork()==0){
        char *child_map = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        char c[12] = "child writes";
        memmove(child_map, c, 12);
        sleep(5);
        memmove(c, child_map, 12);
        if (!strcmp(c, "child writes")){
        } else{
            printf(1, "MAP_SHARED fail\n");
            return;
        }
        memmove(c, &child_map[12], 12);
        if (!strcmp(c, "paren writes")){
        } else {
            printf(1, "MAP_SHARED fail\n");
            return;
        }
        printf(1, "Child exiting...\n");
        exit();
    } else{
        char p[13];
        char *parent_map;
        
        parent_map = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        sleep(2);
        
        memmove(p, parent_map, 12);
        p[12] = '\0';
        if (!strcmp(p, "child writes")){
        } else{
            printf(1, "MAP_SHARED fail\n");
            return;
        }
        strcpy(p, "paren writes");
        memmove(&parent_map[12], p, 12);
        sleep(10);
        printf(1, "MAP_SHARED ok\n");
    }
}

// MAP_PRIVATE test with fork
void mpf(){
    int fd = open("TEST", O_RDWR);
    if (fork() == 0){
        char c[512];
        for (int i=0; i<512; i++){
            c[i] = 'c';
        }
        char *child_map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        if (child_map == MAP_FAILED){
            printf(1, "mmap failed\n");
            return;
        }
        sleep(4);
        printf(1, "char: %c\n", child_map[0]);
        memmove(child_map, c, 512);

        int count = 0;
        for (int i=0; i<512; i++){
            if (child_map[i]=='c'){
                count++;
            }
        }
        if (count!=512){
            printf(1, "MAP_PRIVATE fail\n");
            return;
        }
        exit();
    } else{
        char c[512];
        for (int i=0; i<512; i++){
            c[i] = 'p';
        }
        char *parent_map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (parent_map == MAP_FAILED){
            printf(1, "mmap failed\n");
            return;
        }
        for (int i=0; i<8; i++){
            memmove(parent_map + i*512, c, 512);
        }
        sleep(10);
        memmove(c, parent_map, 512);
        int count = 0;
        for (int i=0; i<512; i++){
            if (c[i]=='p'){
                count++;
            }
        }
        if (count!=512){
            printf(1, "MAP_PRIVATE fail\n");
            return;
        }
        printf(1, "MAP_PRIVATE ok\n");
    }
}

// MAP ANONYMOUS test

// Concurrency test
void concurrency(){
    int fd = open("TEST", O_RDWR);
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED){
        printf(1, "mmap failed\n");
        return;
    }

    if (fork() == 0){
        // a
        if (fork() == 0){
            // b
            for (int i=0; i < 4096; i++){
                if (i%3 == 1){
                    printf(1, "1.%d ", i);
                    map[i] = 'b';
                }
            }
            printf(1, "\n");
            // exit();
        } else{
            for (int i=0; i < 4096; i++){
                if (i%3==0){
                    printf(1, "2.%d ", i);
                    map[i] = 'a';
                }
            }
            printf(1, "\n");
            // exit();
        }
    } else{
        // c
        for (int i=0; i<4096; i++){
            if (i % 3 == 2){
                printf(1, "3.%d ", i);
                map[i] = 'c';
            }
        }
        printf(1, "\n");
        wait();
        wait();
        int countA = 0, countB = 0, countC = 0, weird = 0;
        for (int i=0; i < 4096; i++){
            switch(map[i]){
                case 'a': countA++; break;
                case 'b': countB++; break;
                case 'c': countC++; break;
                default: weird++; break;
            }
        }
        printf(1, "%d %d %d %d\n", countA, countB, countC, weird);
    }
}

// Do the maps get correctly carried across fork?
void forkTestMapShared(){
    // MAP_SHARED
    int fd = open("TEST", O_RDWR);
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (map == MAP_FAILED){
        printf(1, "mmap fail\n");
        return;
    }

    printf(1, "%c\n", map[0]);

    if (fork() == 0){
        printf(1, "In child, initially: %c\n", map[0]);
        map[0] = 'c';
        map[1] = 'd';
    } else{
        sleep(2);
        printf(1, "In parent: %c\n", map[0]);
        if (map[0]=='c'){
            printf(1, "char: %c\n", map[1]);
        } else{
            printf(1, "fork test fail\n");
            return;
        }
    }
    printf(1, "fork test ok\n");
}

void forkTestMapPrivate(){
    // MAP_SHARED
    int fd = open("TEST", O_RDWR);
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

    if (map == MAP_FAILED){
        printf(1, "mmap fail\n");
        return;
    }

    // pagefault 1
    printf(1, "%c\n", map[0]);
    // Parent has done changes before fork. This is mapped privately. But child will see the change
    // pagefault 2: copy-on-write
    map[0] = 'p';

    if (fork() == 0){
        // pagefault 3
        printf(1, "In child, initially: %c\n", map[0]);
        // pagefault 4: copy-on-write
        map[0] = 'c';
        map[1] = 'd';
    } else{
        sleep(2);
        printf(1, "In parent: %c\n", map[0]);
        if (map[0]=='p'){
            printf(1, "char: %c\n", map[1]);
        } else{
            printf(1, "fork test fail\n");
            return;
        }
    }
    printf(1, "fork test ok\n");
}

// Everything, Everywhere (Wrong) All At Once

int main(){
    // smsro();
    // smswo();
    // smsno();
    // smp();
    // if (fork()==0){
    //     smsno();
    // } else{
    //     wait();
    //     printf(1, "PROT_NONE ok\n");
    // }
    // sanity_check();
    // leftVMAtest();
    // rightVMAtest();
    // sandwichTest();
    // msf();
    // mpf();
    // forkTestMapShared();
    // forkTestMapPrivate();
    concurrency();
    exit();
}
