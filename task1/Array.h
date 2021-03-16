#pragma once
#include <stdbool.h>

struct Array
{
    int size;
    int* data;
    bool isSorted;
};


struct Array sortArrayFromFile(const char* filename);
void arrayPrinter(int* array, int size);