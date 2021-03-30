#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>

#include "xmalloc.h"

typedef struct header {
    size_t size;
    struct bucket prev;
    struct bucket next;
} header;

typedef struct bucket {
    header head;    
} bucket;

const size_t PAGE_SIZE = 4096;
static bucket* buckets = 0;

void
initialize_buckets()
{
    buckets = mmap(
            NULL,
            PAGE_SIZE,
            PROT_READ|PROT_WRITE,
            MAP_ANON|MAP_PRIVATE,
            0, 0);
}

long
pow(long base, long exponent)
{
    long result = 1;
    for (int ii = 0; ii < exponent; ii++)
    {
        result *= base;
    }
    return result;
}

long
bucket_index(size_t bytes)
{
    int OFFSET = 3;

    if (bytes < 4)
    {
        return 0;
    }
    else if (bytes < 16)
    {
        return 1;
    }

    long index = 1;

    for (int ii = 2; pow(2, ii + OFFSET) < bytes && pow(2, ii + OFFSET) < PAGE_SIZE; ii++)
    {
        if (pow(2, ii + OFFSET) + pow(2, ii + OFFSET - 1) < ii)
        {
            index++;
        }
        else
        {
            return index;
        }
    }

    return index;
}

void*
get_block(bucket bb)
{
    // TODO Make this
    return 0;
}

void*
xmalloc(size_t bytes)
{
    if (buckets == NULL)
    {
        initialize_buckets();
    }
    
    void* block = get_block(buckets[bucket_index(bytes)]); // TODO make this

    return block;
}

void
xfree(void* ptr)
{
    // TODO: write an optimized free
}

void*
xrealloc(void* prev, size_t bytes)
{
    // TODO: write an optimized realloc
    return 0;
}