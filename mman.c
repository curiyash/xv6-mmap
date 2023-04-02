#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mman.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = 0;
	unsigned int length = 8192;
	int prot=PROT_READ, flags=0, fd=0, offset=0;

	fd = open("README", 0);

	char *ret = (char *) mmap(addr, length, prot, flags, fd, offset);
	if (ret!=(char *) 0xffffffff){
		printf(1, "call to mmap succeeded %x %s\n", ret, ret);
	}
	int stat = munmap((void *) ret, 4096);
	if (stat==0){
		printf(1, "call to munmap succeeded %x\n", ret);
	}
	printf(1, "Hello: %x %c%c%c\n", ret, ret[0], ret[4097], ret[4098]);
	exit();
}

// Should segfault
// map -> unmap -> access => segfault
void test1(){

}
