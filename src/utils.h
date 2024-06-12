// utils.h
// a little library to emulate some nicer arduino features

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <Arduino.h>

// writes an integer to a string
// returns the length of the number in chars
int write_num_to_str(char* str, int num);

// pauses the current thread
void breakpoint();

#endif