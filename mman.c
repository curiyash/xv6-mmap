#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mman.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = 0;
	unsigned int length = 4;
	int prot=0, flags=0, fd=0, offset=4;

	fd = open("README", 0);

	char *ret = (char *) mmap(addr, length, prot, flags, fd, offset);
	if (ret!=0){
		printf(1, "call to mmap succeeded %x %s\n", ret, ret);
	}
	int stat = munmap(addr, length);
	if (stat==0){
		printf(1, "call to munmap succeeded\n");
	}
	exit();
}
