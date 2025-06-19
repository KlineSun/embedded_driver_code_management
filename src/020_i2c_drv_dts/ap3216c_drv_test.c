
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/*
 */
int main(int argc, char **argv)
{
	int fd;
	short buf[3];
	int len;
	

	/* 2. 打开文件 */
	fd = open("/dev/ap3216c_i2c_drv", O_RDWR);
	if (fd == -1)
	{
		printf("can not open file /dev/hello\n");
		return -1;
	}

	len = read(fd, buf, sizeof(buf));		
	printf("APP read : ");
	for (len = 0; len < 3; len++)
		printf("value[%d]=%d\n", len, buf[len]);
	printf("\n");
	
	close(fd);
	
	return 0;
}


