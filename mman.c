#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[]){
	// declare and define all params
	void *addr = (void *) 10000;
	unsigned int length = 21;
	int prot=0, flags=0, fd=0, offset=24;

	int ret = mmap(addr, length, prot, flags, fd, offset);
	if (ret==0){
		printf(1, "call to mmap succeeded\n");
	}
	int stat = munmap(addr, length);
	if (stat==0){
		printf(1, "call to munmap succeeded\n");
	}
	exit();
}
