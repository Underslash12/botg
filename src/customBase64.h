// a custom base64 encoder since nothing else is working

#ifndef BASE64C_H
#define BASE64C_H

#include <stdio.h> 

// encodes an array of bytes
// each byte will occupy 2 bytes in memory
void base64_encode_url(unsigned char* src, size_t src_len, unsigned char* dst, size_t& dst_len);

// not going to bother with a decode since that will be on the python end

#endif