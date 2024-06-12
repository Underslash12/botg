// customBase64.cpp

#include "customBase64.h"

const char* url_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

void base64_encode_url(unsigned char* src, size_t src_len, unsigned char* dst, size_t& dst_len) {
    // chunk the input into 3-byte chunks
    int src_len_no_pad = src_len - (src_len % 3);
    int src_index = 0;
    int dst_index = 0;
    while (src_index < src_len_no_pad) {
        dst[dst_index + 0] = url_table[src[src_index] >> 2];
        // printf("%d\n", src[src_index] >> 2);
        dst[dst_index + 1] = url_table[((src[src_index] & 0x03) << 4) | (src[src_index + 1] >> 4)]; 
        // printf("%d\n", ((src[src_index] << 6) >> 2) | (src[src_index + 1] >> 4));
        dst[dst_index + 2] = url_table[((src[src_index + 1] & 0x0f) << 2) | (src[src_index + 2] >> 6)]; 
        // printf("%d\n", ((src[src_index + 1] << 4) >> 2) | (src[src_index + 2] >> 6));
        dst[dst_index + 3] = url_table[(src[src_index + 2] & 0x3f)]; 
        // printf("%d\n\n", (src[src_index + 2] << 2) >> 2);
        src_index += 3;
        dst_index += 4;
    }

    // deal with the cases when it isn't divisible into 3 
    if (src_len % 3 == 1) {
        // dst_len works out to be 2 (mod 4) for actual data
        dst[dst_index + 0] = url_table[src[src_index] >> 2];
        dst[dst_index + 1] = url_table[src[src_index] & 0x03];
        src_index += 1;
        dst_index += 2;
    }   
    else if (src_len % 3 == 2) {
        // dst_len works out to be 3 (mod 4) for actual data
        dst[dst_index + 0] = url_table[src[src_index] >> 2];
        dst[dst_index + 1] = url_table[((src[src_index] & 0x03) << 4) | (src[src_index + 1] >> 4)];
        dst[dst_index + 2] = url_table[src[src_index + 1] & 0x0f];
        // printf("%d %d %d %c\n", src[src_index + 1], src[src_index + 1] & 0x0f, dst[dst_index + 2], dst[dst_index + 2]);

        // printf("%c %c %c\n", dst[dst_index + 0], dst[dst_index + 1], dst[dst_index + 2]);
        // printf("%d %d %d %d %d %d\n", src[src_index], (src[src_index] & 0x03), ((src[src_index] & 0x03) << 6), src[src_index + 1], (src[src_index + 1] >> 4), ((src[src_index] & 0x03) << 6) | (src[src_index + 1] >> 4));
        src_index += 2;
        dst_index += 3;
    }

    dst[dst_index] = '\0'; 
    dst_len = dst_index;
}