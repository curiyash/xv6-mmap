#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mman.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = 0;
	unsigned int length = 8192;
	int prot=PROT_READ | PROT_WRITE, flags=MAP_PRIVATE, fd=0, offset=0;
	printf(1, "Hola\n");

	fd = open("README", O_RDWR);
	char c[2];

	// read(fd, &c[0], 1);
	// c[1] = '\0';
	// printf(1, "char: %s\n", c);
	// write(fd, &c[0], 1);

	char *ret = (char *) mmap(addr, length, prot, flags, fd, offset);
	if (ret!=(char *) 0xffffffff){
		printf(1, "call to mmap succeeded %x %c %c\n", ret, ret[0], ret[1]);
	}
	printf(1, "This should cause a page fault\n");
	ret[0] = 'b';
	printf(1, "call to mmap succeeded %x %c %c\n", ret, ret[0], ret[1]);
	read(fd, &c[0], 1);
	c[1] = '\0';
	printf(1, "char: %c\n", c[0]);
	// int stat = munmap((void *) ret, 4096);
	// if (stat==0){
	// 	printf(1, "call to munmap succeeded %x\n", ret);
	// }asd
	close(fd);
	exit();
}

// Should segfault
// map -> unmap -> access => segfault
void test1(){

}
