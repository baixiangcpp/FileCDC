#ifndef __UTILS_H
#define __UTILS_H


void rawdata_to_hex (const unsigned char *rawdata, char *hex_str, int n_bytes) ;

int do_write_chunk (const unsigned char *checksum, const char *buf, int len);

ssize_t readn(int fd, void *vptr, size_t n);

#endif
