#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "mman.h"

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

    if (munmap(ret, 4096) == -1){
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

int smsno(){
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
    return -1;
}

void leftVMAtest(){
    int fd = open("README", O_RDONLY);

    char *ret = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);

     if (ret != (char *) 0xffffffff){
        if (ret[0] == 'x'){
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
    if (munmap(ret+4096, 4096) == -1){
        printf(1, "Single process, MAP_SHARED, READ_ONLY fail\n");
        exit();
    }
    printf(1, "Single process, MAP_SHARED, READ_ONLY ok\n");
}

void rightVMAtest(){
    int fd = open("README", O_RDONLY);

    char *ret = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);

    if (ret != (char *) 0xffffffff){
        if (ret[0] == 'x'){
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

// int heloo(){
//     unlink("heloo");
//     int fd = open("heloo2", O_CREATE | O_RDWR);

//     printf(1, "%d\n", fd);
//     char buf[n];
//     for (int i=0; i<n; i++){
//         buf[i] = '1';
//     }
//     for (int i=0; i < 5; i++){
//         printf(1, "%d ", i);
//         write(fd, buf, n);
//     }
//     printf(1, "\n");
//     printf(1, "Hello\n");
//     close(fd);
//     fd = open("heloo2", O_RDWR);

//     int count = 0;
//     for (int i=0; i < 5; i++){
//         read(fd, buf, n);
//         for (int i=0; i < n; i++){
//             if (buf[i] == '1'){
//                 count++;
//             }
//         }
//     }
//     if (count == n * 5){
//         printf(1, "mman_test ok");
//     } else{
//         printf(1, "oops %d\n", count);
//         return 1;
//     }
//     close(fd);
//     unlink("heloo2");
//     return 0;
// }

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
    printf(1, "smp\n");
    int fd = open("README", O_RDWR);
    int status = 0;
    char *read_map = mmap(0, 8192, PROT_READ, MAP_PRIVATE, fd, 0);
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
    printf(1, "After writing: %c | But originally: %c\n", write_map[0], read_map[4096]);
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
    printf(1, "Been there, Done that\n");

    if (fork() == 0){
        // This should page fault and not return
        printf(1, "In child\n");
        if (none_map[0]){
            status = 1;
            printf(1, "status: %d\n", status);
        }
        exit();
    } else{
        sleep(10);
        wait();
        if (status){
            printf(1, "MAP_PRIVATE PROT_NONE fail\n");
            exit();
        }
        printf(1, "I'm good\n");
        if (munmap(read_map, 4096) == -1 || munmap(write_map, 4096) == -1 || munmap(none_map, 4096) == -1 ){
            printf(1, "Unmapping fail\n");
            return;
        }
        printf(1, "MAP_PRIVATE ok\n");
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
    printf(1, "$$$$$$$$$$$$\n");
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
            for (int i=0; i < 20; i++){
                if (i%3 == 0){
                    memmove(&map[i], "a", 1);
                }
            }
            printf(1, "\n");
            exit();
        } else{
            printf(1, "In child %c %c\n", map[0], map[1]);
            for (int i=0; i < 20; i++){
                if (i%3==1){
                    memmove(&map[i], "b", 1);
                }
            }

            printf(1, "Child exited\n");
            exit();
        }
    } else{
        // c
        printf(1, "In parent %c\n", map[1]);
        for (int i=0; i<20; i++){
            if (i%3 == 2){
                memmove(&map[i], "c", 1);
            }
        }
    }

    int pid = wait();
    int pid2 = wait();
    printf(1, "pid: %d | pid2: %d\n", pid, pid2);
    int countA = 0, countB = 0, countC = 0, weird = 0;
    for (int i=0; i < 20; i++){
        switch(map[i]){
            case 'a': countA++; break;
            case 'b': countB++; break;
            case 'c': countC++; break;
            default: weird++; break;
        }
    }

    if (munmap(map, 4096) == -1){
        printf(1, "munmap fail\n");
        exit();
    }

    if (countA==7 && countB == 7 && countC == 6){
        printf(1, "concurrency ok\n");
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
        exit();
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

    if (munmap(map, 4096) == -1){
        printf(1, "munmap fail\n");
        exit();
    }
    printf(1, "fork test ok\n");
}

void mapPrivateCorrectness(){
    int fd = open("README", O_RDWR);
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // Initial
    printf(1, "char: %c\n", map[0]);
    map[0] = 'p';

    if (fork() == 0){
        if (map[0] == 'p'){
        } else{
            printf(1, "MAP_PRIVATE incorrect\n");
            exit();
        }
        map[0] = 'c';
        exit();
    } else{
        wait();
        if (map[0] =='p'){
            printf(1, "MAP_PRIVATE correctness ok\n");
        } else{
            printf(1, "MAP_PRIVATE correctness fail\n");
            exit();
        }
        
    }
}

// MAP_ANONYMOUS
void sma(){
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    char *map2 = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    if (map == MAP_FAILED || map2 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    memmove(map2, "pppp", 4);

    if (fork() == 0){
        for (int i=0; i < 20; i++){
            if (i % 2 == 0){
                memmove(&map[i], "c", 1);
            }
        }
        printf(1, "char child: %c\n", map[0]);
        printf(1, "private map: %s\n", map2);
        exit();
    } else {
        for (int i=0; i < 20; i++){
            if (i % 2 == 1){
                memmove(&map[i], "p", 1);
            }
        }
        printf(1, "char parent: %c\n", map[1]);
        wait();
        int countC = 0, countP = 0;
        for (int i=0; i < 4096; i++){
            switch(map[i]){
                case 'c': countC++; break;
                case 'p': countP++; break;
                default: break;
            }
        }

        if ((countC == countP) && countC == 10){
            printf(1, "MAP_ANONYMOUS ok\n");
        } else{
            printf(1, "MAP_ANONYMOUS fail\n");
        }
    }
}

// Read-Write consistency
// mmap write
// file read
// file write
// mmap read
void readWriteTest(){
    int fd = open("README", O_RDWR);
    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char c[5], buf[5];
    int pid;

    if (map == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    // Initial content
    memmove(c, map, 4);
    c[4] = '\0';
    printf(1, "Initial: %s\n", c);

    if ((pid = fork()) == 0){
        // mmap write
        memmove(&map[0], "mmap", 4);

        sleep(10);
        // mmap read
        memmove(c, &map[4], 4);

        if (!strcmp(c, "file")){
            printf(1, "File write consistency ok\n");
        } else{
            printf(1, "File write consistency check fail\n");
        }
        exit();
    } else{
        // fileread
        sleep(2);
        read(fd, buf, 4);
        if (!strcmp(buf, "mmap")){
            printf(1, "File read consistency ok\n");
        } else{
            kill(pid);
            wait();
            printf(1, "File read consistency check fail\n");
            exit();
        }
        
        // File write
        memmove(buf, "file", 4);
        write(fd, buf, 4);

        int pid = wait();
        printf(1, "pid: %d\n", pid);
    }
}

void
fourfiles(void)
{
    char buf[512];
  int fd, pid, i, j, n, total, pi;
  char *names[] = { "f0", "f1", "f2", "f3" };
  char *fname;
  int factor = 1;

  printf(1, "fourfiles test\n");

  for(pi = 0; pi < 4; pi++){
    fname = names[pi];
    unlink(fname);

    pid = fork();
    if(pid < 0){
      printf(1, "fork failed\n");
      exit();
    }

    if(pid == 0){
      fd = open(fname, O_CREATE | O_RDWR);
      if(fd < 0){
        printf(1, "create failed\n");
        exit();
      }

      memset(buf, '0'+pi, 512);
      for(i = 0; i < factor; i++){
        if((n = write(fd, buf, 512)) != 512){
          printf(1, "write failed %d\n", n);
          exit();
        }
      }
      exit();
    }
  }

  for(pi = 0; pi < 4; pi++){
    wait();
  }

  for(i = 0; i < 2; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0 && total < 512 * factor){
      for(j = 0; j < n; j++){
        if(buf[j] != '0'+i){
          printf(1, "wrong char %d %d %d\n", total, j, n);
          exit();
        }
      }
      total += n;
    }
    close(fd);
    if(total != factor*512){
      printf(1, "wrong length %d\n", total);
      exit();
    }
    unlink(fname);
  }

  printf(1, "fourfiles ok\n");
}

void sharedfd(void){
  int fd, pid, i, n, nc, np;
  char buf[10];
  int factor = 400;

  printf(1, "sharedfd test\n");

  unlink("sharedfd");
  fd = open("sharedfd", O_CREATE|O_RDWR);
  if(fd < 0){
    printf(1, "fstests: cannot open sharedfd for writing");
    return;
  }
  pid = fork();
  memset(buf, pid==0?'c':'p', sizeof(buf));
  for(i = 0; i < factor; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fstests: write sharedfd failed\n");
      break;
    }
  }
  if(pid == 0)
    exit();
  else
    wait();
  close(fd);
  fd = open("sharedfd", 0);
  if(fd < 0){
    printf(1, "fstests: cannot open sharedfd for reading\n");
    return;
  }
  nc = np = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i = 0; i < sizeof(buf); i++){
      if(buf[i] == 'c')
        nc++;
      if(buf[i] == 'p')
        np++;
    }
  }
  close(fd);
//   unlink("sharedfd");
  printf(1, "%d %d\n", nc, np);
  if(nc == 10*factor && np == 10*factor){
    printf(1, "sharedfd ok\n");
  } else {
    printf(1, "sharedfd oops %d %d\n", nc, np);
    exit();
  }
}

void sms(){
    smsro();
    smswo();
    int status = 0;
    if (fork() == 0){
        status = smsno();
        exit();
    } else{
        wait();
        if (status==-1){
            printf(1, "MAP_SHARED PROT_NONE fail\n");
        } else{
            printf(1, "MAP_SHARED PROT_NONE ok\n");
        }
    }
}

// Everything, Everywhere (Wrong) All At Once
void EveryThingEverywhereAllAtOnce(){
    // Input checking
    // Incorrect open access intent and map prot
    int fd = open("README", O_RDONLY);
    int status = 0;

    char *map = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED){
    } else{
        printf(1, "File access and Prot check should have failed\n");
        exit();
    }

    // Unmapping a map that doesn't exist
    if (munmap(map, 4096) == -1){
    } else{
        printf(1, "Unmapping unmapped region\n");
        exit();
    }

    // Incorrect flags
    map = mmap(0, 1, PROT_READ, MAP_SHARED | MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED){
    } else{
        printf(1, "Mapping Shared as well as Private map not allowed\n");
        exit();
    }

    // Anonymous without Private or Shared
    map = mmap(0, 1, PROT_READ, MAP_ANONYMOUS, fd, 0);
    if (map == MAP_FAILED){
    } else{
        printf(1, "Anonymous mapping without Shared or Private flag\n");
        exit();
    }

    // Incorrect length
    map = mmap(0, -1, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED){
    } else{
        printf(1, "Incorrect length\n");
        exit();
    }

    // Offset not page-aligned
    map = mmap(0, 1, PROT_READ, MAP_PRIVATE, fd, 1);
    if (map == MAP_FAILED){
    } else{
        printf(1, "Offset not page-aligned\n");
        exit();
    }

    // Trying to map stdout
    map = mmap(0, 1, PROT_READ, MAP_PRIVATE, 1, 1);
    if (map == MAP_FAILED){
    } else{
        printf(1, "Mapped standard output: fail\n");
        exit();
    }

    // A successful map
    map = mmap(0, 1, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    } else{
        // Trying to write to read-only map
        if (fork() == 0){
            printf(1, "Hello\n");
            map[0] = 'a';
            status = 1;
            exit();
        } else{
            wait();
            if (status){
                printf(1, "Read-only write fail\n");
                exit();
            }
        }
    }

    // Unmapping already unmapped map
    if (munmap(map, 4096) == -1){
        printf(1, "munmap fail\n");
        exit();
    }

    /*
        It is not an error if the indicated range does not contain any mapped pages
    */
    if (munmap(map, 4096) == 0){
    } else{
        printf(1, "munmap fail\n");
        exit();
    }
    printf(1, "Everything Everywhere ok\n");
}

void maxVMATest(){
    // Maximum VMA that can be allotted are 10
    char *map1 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map1 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map2 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map2 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map3 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map3 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map4 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map4 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map5 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map5 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map6 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map6 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map7 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map7 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map8 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map8 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map9 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map9 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map10 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map10 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }

    // This should fail
    char *map11 = mmap(0, 4096, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, 0, 0);
    if (map11 == MAP_FAILED){
        printf(1, "Max VMA test ok\n");
    } else{
        printf(1, "Max VMA test fail\n");
        exit();
    }
}

void maxVMAMunmapTest(){
    int fd = open("README", O_RDONLY);
    char *map1 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map1 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map2 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map2 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map3 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map3 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map4 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map4 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map5 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map5 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map6 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map6 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map7 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map7 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map8 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map8 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map9 = mmap(0, 8192, PROT_READ, MAP_SHARED, fd, 0);
    if (map9 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }
    char *map10 = mmap(0, 4096*3, PROT_READ, MAP_SHARED, fd, 0);
    if (map10 == MAP_FAILED){
        printf(1, "mmap fail\n");
        exit();
    }

    // All these should be successful
    int res = munmap(map1, 4096) | munmap(map2, 4096) | munmap(map3, 4096) | munmap(map4, 4096) | munmap(map5, 4096) | munmap(map6, 4096) | munmap(map7, 4096) | munmap(map8, 4096) | munmap(map9, 4096);

    if (res == -1){
        printf(1, "munmap fail\n");
        exit();
    }

    // Unmapping of map10 should fail => Not enough VMAs
    if (munmap(map10+4096, 4096) == -1){
        printf(1, "maxVMAMunmap ok\n");
    } else{
        printf(1, "munmap fail\n");
        exit();
    }
}

int main(){
    // sms();
    // smp();
    // leftVMAtest();
    // rightVMAtest();
    // sandwichTest();
    // msf();
    // mpf();
    // forkTestMapShared();
    // forkTestMapPrivate();
    // concurrency();
    // mapPrivateCorrectness();
    // sma();
    // readWriteTest();
    // fourfiles();
    // sharedfd();
    // EveryThingEverywhereAllAtOnce();
    // maxVMATest();
    maxVMAMunmapTest();
    exit();
}
