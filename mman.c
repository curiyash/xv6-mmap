#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mman.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = 0;
	unsigned int length = 8192;
	int prot=PROT_READ | PROT_WRITE, flags=MAP_SHARED, fd=0, offset=0;
	printf(1, "Hola\n");

	fd = open("README", O_RDWR);

	// Idea is to map as MAP_ANON
	char *ret = mmap(addr, length, prot, flags, fd, offset);

	printf(1, "char: %c\n", ret[0]);
	printf(1, "############################\n");
	printf(1, "char: %c\n", ret[4096]);

	// munmap(ret, 4096);

	// printf(1, "char: %c\n", ret[0]);

	close(fd);
	exit();
}

// Should segfault
// map -> unmap -> access => segfault
void test1(){

}
