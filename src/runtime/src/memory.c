#include <stddef.h>
#include "platform.h"

#if RSTD_RUNTIME_WASM

__attribute__((optnone)) void* memcpy(void* destination, const void* source, size_t size) {
    unsigned char*       output = destination;
    const unsigned char* input  = source;
    for (size_t index = 0; index < size; ++index) output[index] = input[index];
    return destination;
}

__attribute__((optnone)) void* memmove(void* destination, const void* source, size_t size) {
    unsigned char*       output = destination;
    const unsigned char* input  = source;
    if (output < input) {
        for (size_t index = 0; index < size; ++index) output[index] = input[index];
    } else if (output > input) {
        for (size_t index = size; index != 0; --index) output[index - 1] = input[index - 1];
    }
    return destination;
}

__attribute__((optnone)) void* memset(void* destination, int value, size_t size) {
    unsigned char* output = destination;
    for (size_t index = 0; index < size; ++index) output[index] = (unsigned char)value;
    return destination;
}

__attribute__((optnone)) int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char* left_bytes  = left;
    const unsigned char* right_bytes = right;
    for (size_t index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index])
            return (int)left_bytes[index] - (int)right_bytes[index];
    }
    return 0;
}

#endif
