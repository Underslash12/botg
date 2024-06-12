// utils.cpp

#include "utils.h"

int write_num_to_str(char* str, int num) {
    // how many characters the number takes up 
    int len = 0;
    int num_copy = num;
    while (num_copy != 0) {
        num_copy /= 10;
        len++;
    }       
    if (len == 0) {
        len = 1;
    }

    // write the number to the string in reverse order
    for (int i = len - 1; i >= 0; i--) {
        str[i] = num % 10 + '0';
        num /= 10;
    }

    return len;
}

void breakpoint() {
    while (1) delay(1000);
}