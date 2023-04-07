#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mman.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = 0;
	unsigned int length = 8192;
	int prot=PROT_READ | PROT_WRITE, flags=0, fd=0, offset=0;

	fd = open("README", O_RDWR);

	char *ret = (char *) mmap(addr, length, prot, flags, fd, offset);
	// close(fd);
	if (ret!=(char *) 0xffffffff){
		printf(1, "call to mmap succeeded %x %c\n", ret, ret[0]);
	}
	// int stat = munmap((void *) ret, 4096);
	// if (stat==0){
	// 	printf(1, "call to munmap succeeded %x\n", ret);
	// }asd
	exit();
}

// Should segfault
// map -> unmap -> access => segfault
void test1(){

}
