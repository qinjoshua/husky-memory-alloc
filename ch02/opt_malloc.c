#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>

#include "xmalloc.h"

typedef struct bucket {
    size_t bucket_size;
    struct bucket* prev;
    struct bucket* next;
    // By the way, the bytemap is going to be 128 bytes.
} bucket;

static const size_t PAGE_SIZE = 4096;
static const size_t BYTEMAP_SIZE = 128;

static const char FREE_MEM = 'f';
static const char ALLOC_MEM = 'a';

static bucket* buckets = 0;
static int POSSIBLE_BLOCK_SIZES_LEN = 18;
static int MAX_BLOCK_SIZE = 3072;
static const long POSSIBLE_BLOCK_SIZES[] = {4, 8, 16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072};

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
block_size_at_index(long index)
{
    assert(index < POSSIBLE_BLOCK_SIZES_LEN);
    return POSSIBLE_BLOCK_SIZES[index];
}

// Sizes go from 4, 8, 16, 24, 32, 48, 64, 96, 128 ...
long
bucket_index(size_t bytes)
{
    assert(bytes <= MAX_BLOCK_SIZE);

    for (int ii = 0; ii < POSSIBLE_BLOCK_SIZES_LEN; ii++)
    {
        if (POSSIBLE_BLOCK_SIZES[ii] > bytes)
        {
            return POSSIBLE_BLOCK_SIZES[ii];
        }
    }

    return NULL;
}

bucket*
get_new_bucket(size_t block_size, bucket* prev, bucket* next)
{
    int numPages = 1;
    const float WASTE_THRESHOLD = 0.125;
    const int PAGE_MAP_LIMIT;
    long bucketSize = numPages * PAGE_SIZE;

    // This one finds how many pages we need. We calculate the overall bucketsize - overhead and see if that exceeds
    // the waste threshold
    while (bucketSize - sizeof(bucket) - BYTEMAP_SIZE > WASTE_THRESHOLD * bucketSize)
    {
        numPages++;
        bucketSize = numPages * PAGE_SIZE;
    }

    bucket* newBucket = mmap(
            NULL,
            PAGE_SIZE,
            PROT_READ|PROT_WRITE,
            MAP_ANON|MAP_PRIVATE,
            0, 0);
    
    newBucket->bucket_size = bucketSize;
    newBucket->prev = prev;
    newBucket->next = next;

    // This is where our bytemap goes
    long numBlocks = (bucketSize - sizeof(bucket) - BYTEMAP_SIZE) / block_size;
    assert(numBlocks <= BYTEMAP_SIZE);
    for (int ii = 0; ii < numBlocks; ii++)
    {
        *(char*)((void*)newBucket + sizeof(bucket) + ii) = FREE_MEM;
    }

    return newBucket;
}

void*
get_block(size_t block_size, bucket* bb)
{
	//while bitmap still has not been fully traversed and current value 
	//is not empty, check next element in bitmap
    long numBlocks = (bb->bucket_size - sizeof(bucket) - BYTEMAP_SIZE) / block_size;
	for (int ii = 0; ii < numBlocks; ii++)
    {
        char* byte = (char*)((void*)bb + sizeof(bucket) + ii);
        if (*byte == FREE_MEM)
        {
            *byte = ALLOC_MEM;
            return (void*)bb + sizeof(bucket) + BYTEMAP_SIZE + (ii * block_size);
        }
    }

    if (bb->next != NULL)
    {
        return get_block(block_size, bb->next);
    }
    else
    {
        // Get new bucket
        bucket* newBucket = get_new_bucket(block_size, bb, 0);
        bb->next = newBucket;

        return get_block(block_size, newBucket);
    }
}

void*
xmalloc(size_t bytes)
{
    if (buckets == NULL)
    {
        initialize_buckets();
    }

    // This is the index in the buckets array that our free memory should be at
    long index = bucket_index(bytes);
    size_t block_size = block_size_at_index(index);

    if (buckets[index] == NULL)
    {
        buckets[index] = get_new_bucket(block_size, NULL, NULL);
    }

    void* block = get_block(block_size, buckets[index]);

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
