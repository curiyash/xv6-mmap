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

      memset(buf, '0'+pi, 512);
      for(i = 0; i < 12; i++){
        if((n = write(fd, buf, 500)) != 500){
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

  printf(1, "!!!!!!!!!!!!!!! In parent\n");

  for(i = 0; i < 2; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0){
      for(j = 0; j < n; j++){
        if(buf[j] != '0'+i){
          printf(1, "wrong char\n");
          exit();
        }
      }
      total += n;
    }
    close(fd);
    printf(1, "total: %d\n", total);
    if(total != 12*500){
      printf(1, "wrong length %d\n", total);
      exit();
    }
    unlink(fname);
  }

  printf(1, "fourfiles ok\n");
}

int main(int argc, char *argv[]){
	fourfiles();
	exit();
}
