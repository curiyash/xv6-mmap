#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
// #include "mman.h"
#include "fcntl.h"

char buf[8192];

void
fourfiles(void)
{
  int fd, pid, i, j, n, total, pi;
  char *names[] = { "f0", "f1", "f2", "f3" };
  char *fname;

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
      int t = 0;
      memset(buf, '0'+pi, 512);
      for(i = 0; i < 12; i++){
        if((n = write(fd, buf, 500)) != 500){
          printf(1, "write failed %d\n", n);
          exit();
        }
        t += n;
      }
      exit();
    }
  }

  for(pi = 0; pi < 4; pi++){
    wait();
  }

  for(i = 0; i < 4; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0){
      for(j = 0; j < n; j++){
        printf(1, "%d ", j);
        if(buf[j] != '0'+i){
          printf(1, "%c %c\n", buf[j], '0' + i);
          printf(1, "wrong char\n");
          exit();
        }
      }
      printf(1, "\n");
      total += n;
    }
    close(fd);
    if(total != 12*500){
      printf(1, "wrong length %d\n", total);
      exit();
    }
    unlink(fname);
  }

  printf(1, "fourfiles ok\n");
}

void
sharedfd(void)
{
  int fd, pid, i, n, nc, np;
  char buf[10];

  printf(1, "sharedfd test\n");

  unlink("sharedfd");
  fd = open("sharedfd", O_CREATE|O_RDWR);
  if(fd < 0){
    printf(1, "fstests: cannot open sharedfd for writing");
    return;
  }
  pid = fork();
  memset(buf, pid==0?'c':'p', sizeof(buf));
  printf(1, "writing\n");
  for(i = 0; i < 100; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf(1, "fstests: write sharedfd failed\n");
      break;
    }
  }
  printf(1, "child exiting\n");
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
  printf(1, "reading\n");
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i = 0; i < sizeof(buf); i++){
      if(buf[i] == 'c')
        nc++;
      if(buf[i] == 'p')
        np++;
    }
  }
  close(fd);
  // unlink("sharedfd");
  printf(1, "%d %d\n", nc, np);
  if(nc == 1000 && np == 1000){
    printf(1, "sharedfd ok\n");
  } else {
    printf(1, "sharedfd oops %d %d\n", nc, np);
    exit();
  }
}

int main(int argc, char *argv[]){
	fourfiles();
  // sharedfd();
	exit();
}
