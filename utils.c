#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "utils.h"

ssize_t readn(int fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

void rawdata_to_hex (const unsigned char *rawdata, char *hex_str, int n_bytes)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < n_bytes; i++) {
        unsigned int val = *rawdata++;
        *hex_str++ = hex[val >> 4];
        *hex_str++ = hex[val & 0xf];
    }
    *hex_str = '\0';
}

int do_write_chunk (const unsigned char *checksum, const char *buf, int len)
{
    char chksum_str[41];
    int fd;
    int n;

    rawdata_to_hex (checksum, chksum_str, 20);

    /* Don't write if the block already exists. */
    if (g_access (chksum_str, F_OK) == 0)
        return 0;

    fd = open (chksum_str, O_WRONLY|O_CREAT,0666);
    if (fd == -1) {
        printf ("Failed to open block %s.\n", chksum_str);
        return -1;
    }

    n = write (fd, buf, len);
    if (n < 0) {
        printf ("Failed to write chunk %s.\n", chksum_str);
        close (fd);
        return -1;
    }

    close(fd);

    return 0;
}