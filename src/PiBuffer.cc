#include "PiBuffer.h"
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

// Constructor
StaticBuffer::StaticBuffer() : values(NULL), alloc_size(0) {
}

// Destructor
StaticBuffer::~StaticBuffer() {
    if (values) free(values);
}

int StaticBuffer::realloc(size_t size) {
    if (values) {
        free(values);
        values = NULL;
        alloc_size = 0;
    }

    values = (uint8_t*)malloc(size);
    if (values == NULL) {
        return ENOMEM;
    }

    alloc_size = size;
    return 0;
}

// Constructor
DinamicBuffer::DinamicBuffer() : values(NULL), offset(0), alloc_size(0) {}

// Destructor
DinamicBuffer::~DinamicBuffer() {
    free(values);
}

int DinamicBuffer::append(void* data, size_t size) {
        size_t remaining = alloc_size - offset;
        if (remaining < size) {
            size_t new_size = alloc_size + (size * 2); // allocate two fragments once.
            uint8_t* tmp = (uint8_t*)realloc(values, new_size);
            if (tmp != NULL) {
                values = tmp;
                alloc_size = new_size;
            } else {
                return ENOMEM;
            }
        }

        memcpy(values + offset, data, size);
        offset += size;
        return 0;
    }

// Reset the offset for writing the next fragment
void DinamicBuffer::resetOffset() {
    offset = 0;
}
