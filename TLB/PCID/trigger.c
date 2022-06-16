#include <stdint.h>
#include <stdio.h>

#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(int argc, char *argv[])
{
	int fd = open("/dev/mmuctl", O_RDONLY);
	return 0;
}
