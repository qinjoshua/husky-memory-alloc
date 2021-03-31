#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>

#include "xmalloc.h"

static const long MAGIC_NUMBER = 720720720817817817;

typedef struct bucket {
  long magic_number;
  size_t block_size;
  size_t bucket_size;
  struct bucket* prev;
  struct bucket* next;
  // By the way, the bytemap is going to be 128 bytes.
} bucket;


typedef struct arena {
  bucket** buckets;
  pthread_mutex_t lock;
} arena;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static arena* arenas;
static const size_t PAGE_SIZE = 4096;
static const size_t BYTEMAP_SIZE = 128;

static __thread int ARENA_ID = -1;

static int NUM_ARENAS = 4;

static int POSSIBLE_BLOCK_SIZES_LEN = 18;
static int MAX_BLOCK_SIZE = 3072;
static const long POSSIBLE_BLOCK_SIZES[] = {4,   8,   16,   24,   32,   48,
                                            64,  96,  128,  192,  256,  384,
                                            512, 768, 1024, 1536, 2048, 3072};


long get_arena_id() {
  if(ARENA_ID == -1 || pthread_mutex_trylock(&(arenas[ARENA_ID].lock)) != 0) {
    //find an open arena 
    for(int ii = 0; ii < NUM_ARENAS; ii++) {
      if(pthread_mutex_trylock(&(arenas[ii].lock)) == 0) {
        ARENA_ID = ii;
        return ARENA_ID;
      }
    }
  }
  return ARENA_ID;
}

void* initialize_buckets() {
  return mmap(NULL,
                 PAGE_SIZE,  // TODO?sizeof(bucket*) * POSSIBLE_BLOCK_SIZES_LEN
                 PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, 0, 0);
}

void initialize_arenas() {
  arenas = mmap(NULL, NUM_ARENAS*sizeof(bucket**), PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, 0,0);
  
  pthread_mutex_lock(&lock);
  for(int ii = 0; ii < NUM_ARENAS; ii++) {
    arenas[ii].buckets = (bucket**)initialize_buckets();
  }
  pthread_mutex_unlock(&lock);
}

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

long block_size_at_index(long index) {
  assert(index < POSSIBLE_BLOCK_SIZES_LEN);
  return POSSIBLE_BLOCK_SIZES[index];
}

// Sizes go from 4, 8, 16, 24, 32, 48, 64, 96, 128 ...
long bucket_index(size_t bytes) {
  assert(bytes <= MAX_BLOCK_SIZE);

  for (int ii = 0; ii < POSSIBLE_BLOCK_SIZES_LEN; ii++) {
    if (POSSIBLE_BLOCK_SIZES[ii] > bytes) {
      return ii;
    }
  }

  return -1;
}

bucket* get_new_bucket(size_t block_size, bucket* prev, bucket* next) {
  int numPages = 1;
  const float WASTE_THRESHOLD = 0.125;
  long bucketSize = numPages * PAGE_SIZE;

  // This one finds how many pages we need. We calculate the overall bucketsize
  // - overhead and see if that exceeds the waste threshold
  while ((bucketSize - sizeof(bucket) - BYTEMAP_SIZE) % block_size >
         WASTE_THRESHOLD * bucketSize) {
    numPages++;
    bucketSize = numPages * PAGE_SIZE;
  }

  bucket* newBucket =
      mmap(NULL,
           bucketSize,  // shouldnt this be bucket size
           PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, 0, 0);

  newBucket->magic_number = MAGIC_NUMBER;
  newBucket->block_size = block_size;
  newBucket->bucket_size = bucketSize;
  newBucket->prev = prev;
  newBucket->next = next;

  // This is where our bitmap goes
  // long numBlocks = (bucketSize - sizeof(bucket) - BYTEMAP_SIZE) / block_size;
  // assert(numBlocks <= BYTEMAP_SIZE);
  // for (int ii = 0; ii < numBlocks; ii+=sizeof(long) * 8) // TODO logic here
  // needs to be clarified!
  // {
  //     // We set all of the bytes here to zero, indicating that they are free
  //     void* blockPointer = (void*)newBucket + sizeof(bucket) + (ii / 8);
  //     *(uint32_t*)blockPointer = 0;
  // }
  // TODO memset

  return newBucket;
}

int get_bit(uint32_t block, int k) { return (block & (1 << k)) >> k; }

void* get_block(bucket* bb) {
  long numBlocks =
      (bb->bucket_size - sizeof(bucket) - BYTEMAP_SIZE) / bb->block_size;

  // There are two loops, an outer and an inner. The outer loop increments by
  // the size of an unsigned 32 bit integer multiplied by the number of bits in
  // a byte, which is 8.
  for (int ii = 0; ii < numBlocks;
       ii += sizeof(uint32_t) * 8)  // TODO logic here needs to be certain!
  {
    // ii iterates over bytes, jj iterates over bits

    // Our bytePointer points to the bucket plus the overhead, plus outer loop
    // index divided by the size of a byte (because to the system the size 1 = 1
    // byte)

    void* bytePointer = (void*)bb + sizeof(bucket) + (ii / 8);

    // This flag is going to be used later to set memory to allocated in the
    // loop
    uint32_t flag = 1;

    // Our inner for loop goes through each individual bit in an unsigned 32 bit
    // integer
    for (int jj = 0; jj < 8 * sizeof(uint32_t); jj++) {
      // For each jj, we get value of the bit in our integer and see if it's
      // free.
      if (get_bit(*(uint32_t*)bytePointer, jj) == 0) {
        *(uint32_t*)bytePointer =
            jj |
            flag;  // Sets the flag at the jj position of our blockpointer to 1
        return (void*)bb + sizeof(bucket) + BYTEMAP_SIZE +
               ((ii + jj) * bb->block_size);  // this should HOPEFULLY return
                                              // the correct memory address
      }
      flag = flag << 1;  // Left-shift the flag so that it can be set at the
                         // next position
    }
  }

  // If code execution has reached here that means there was no free memory

  if (bb->next != NULL) {
    return get_block(bb->next);
  } else {
    // Get new bucket
    bucket* newBucket = get_new_bucket(bb->block_size, bb, 0);
    bb->next = newBucket;

    return get_block(newBucket);
  }
}

void* xmalloc(size_t bytes) {

  if(bytes > 3072) {
    long pages_needed = div_up(bytes, PAGE_SIZE);
    return mmap(NULL, pages_needed * PAGE_SIZE, PROT_READ | PROT_WRITE,
      MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
  }
  else {
    if (arenas == NULL) {
      initialize_arenas();
    }
    // if (buckets == NULL) {
    //   initialize_buckets();
    // }

    // This is the index in the arena and buckets array that our free memory should be at
    // ALSO locks
    long arena_id = get_arena_id();
    long index = bucket_index(bytes);
    size_t block_size = block_size_at_index(index);

    bucket** buckets = arenas[arena_id].buckets;
    assert(buckets != NULL);
    
    if (buckets[index] == NULL)  // see if magic number is there or not?
    {
      // getnewbucket inits a new bucket
        buckets[index] = get_new_bucket(block_size, NULL, NULL);
    }

    void* block = get_block(buckets[index]);

    pthread_mutex_unlock(&(arenas[arena_id].lock));
    return block;
  }
}

void xfree(void* ptr) {
  uint64_t address = (uint64_t)ptr;
  void* pageStart = (void*) (address - (address % PAGE_SIZE));

  // If we cast it to a long, does it have that magic number?
  while (*(long*)pageStart != MAGIC_NUMBER) {
    pageStart -= PAGE_SIZE;
  }

  // Pointer arithmetic to free it in our bytemap
  bucket* bb = (bucket*)pageStart;

  // The block number of this block
  int blockNo =
      (ptr - (pageStart + sizeof(bucket) + BYTEMAP_SIZE)) / bb->block_size;

  // This is the offset from the page start + header to get to the unsigned 32
  // bit integer that contains the flag for the memory
  int bitmapAddressOffset = blockNo / 8 / sizeof(uint32_t);

  // This is the address of the unsigned 32 bit integer that contains the flag
  // for the memory
  void* bitmapAddress = pageStart + sizeof(bucket) +  // This is bucket overhead
                        bitmapAddressOffset;

  // This gives us the exact bit offset within a 32 bit unsigned integer that
  // our block is located
  int internalOffset = blockNo % (bitmapAddressOffset * 8 * sizeof(uint32_t));

  uint32_t flag = 1 << internalOffset;
  *(uint32_t*)bitmapAddress = *(uint32_t*)bitmapAddress ^ flag;

  // TODO Check to see if we should munmap this page

  // num_blocks

  long numBlocks =
      (bb->bucket_size - sizeof(bucket) - BYTEMAP_SIZE) / bb->block_size;

  int any_left = 0;

  for (int ii = 0; ii < numBlocks;
       ii += sizeof(uint32_t) * 8)  // TODO logic here needs to be certain!
  {
    // ii iterates over bytes, jj iterates over bits

    // Our bytePointer points to the bucket plus the overhead, plus outer loop
    // index divided by the size of a byte (because to the system the size 1 = 1
    // byte)

    void* bytePointer = (void*)bb + sizeof(bucket) + (ii / 8);

    // This flag is going to be used later to set memory to allocated in the
    // loop
    uint32_t flag = 1;

    // Our inner for loop goes through each individual bit in an unsigned 32 bit
    // integer
    for (int jj = 0; jj < 8 * sizeof(uint32_t); jj++) {
      // For each jj, we get value of the bit in our integer and see if it's
      // free.
      if (get_bit(*(uint32_t*)bytePointer, jj) == 1) {
        any_left = 1;
        break;
      }
      flag = flag << 1;  // Left-shift the flag so that it can be set at the
                         // next position
    }
  }

  if (!any_left) {
    munmap(pageStart, bb->bucket_size);
  }
}

void* xrealloc(void* prev, size_t bytes) {
  // TODO: write an optimized realloc
  uint64_t address = (uint64_t)prev;
  void* pageStart = (void*) (address - (address % PAGE_SIZE));

  // If we cast it to a long, does it have that magic number?
  while (*(long*)pageStart != MAGIC_NUMBER) {
    pageStart -= PAGE_SIZE;
  }

  // Pointer arithmetic to free it in our bytemap
  bucket* bb = (bucket*)pageStart;


  void* new_ptr = xmalloc(bytes);

  memcpy(new_ptr, prev, bb->block_size);

  return new_ptr;
}