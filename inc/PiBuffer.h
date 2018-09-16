#pragma once

#include <stdio.h>
#include <stdint.h>

class StaticBuffer {
public:
    uint8_t* values;
    size_t alloc_size;

    StaticBuffer();
    ~StaticBuffer();

    int realloc(size_t size);
};

class DinamicBuffer {
public:
    uint8_t* values;
    size_t offset;
    size_t alloc_size;

    DinamicBuffer();
    ~DinamicBuffer();

    int append(void* data, size_t size);
    void resetOffset();
};

