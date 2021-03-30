#include <stdlib.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>

#include "xmalloc.h"

// TODO: This file should be replaced by another allocator implementation.
//
// If you have a working allocator from the previous HW, use that.
//
// If your previous homework doesn't work, you can use the provided allocator
// taken from the xv6 operating system. It's in xv6_malloc.c
//
// Either way:
//  - Replace xmalloc and xfree below with the working allocator you selected.
//  - Modify the allocator as nessiary to make it thread safe by adding exactly
//    one mutex to protect the free list. This has already been done for the
//    provided xv6 allocator.
//  - Implement the "realloc" function for this allocator.

typedef struct xm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} xm_stats;

typedef struct free_block {
    long size;
    struct free_block* next;
} free_block;

const size_t PAGE_SIZE = 4096;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static xm_stats stats; // This initializes the stats to 0.
static free_block* free_list = 0;

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

void
insert_free_block(void* address, long size)
{
    free_block* next_block = free_list;
    free_block* parent_block = NULL;

    while (next_block != NULL && address > (void*)next_block)
    {
        parent_block = next_block;
        next_block = next_block->next;
    }

    free_block* newly_freed = (free_block*)address;

    newly_freed->size = size;
    newly_freed->next = next_block;
    
    if (parent_block == NULL) //If it's NULL
    { 
        free_list = newly_freed;
        parent_block = newly_freed;

        if ((void*)parent_block + parent_block->size + sizeof(size_t) == (void*)next_block) // Coalesce if it happens to be that two blocks of freed memory are next to each other
        {
            parent_block->size = parent_block->size + next_block->size + sizeof(size_t); // Change the size to encompass both blocks
            parent_block->next = next_block->next; // Skip over the current next
        }
    }
    else // Otherwise, make it a child of the last parent block that we saw
    {
        parent_block->next = newly_freed;

        if ((void*)newly_freed + newly_freed->size + sizeof(size_t) == (void*)newly_freed->next)
        {
            newly_freed->size = newly_freed->size + newly_freed->next->size + sizeof(size_t);
            newly_freed->next = newly_freed->next->next;
        }

        if ((void*)parent_block + parent_block->size + sizeof(size_t) == (void*)newly_freed)
        {
            parent_block->size = parent_block->size + newly_freed->size + sizeof(size_t); // Change the size to encompass both blocks
            parent_block->next = newly_freed->next; // Skip over the recently freed memory next
        }
    }
}

void*
xmalloc(size_t bytes)
{
    pthread_mutex_lock(&lock);

    size_t size = bytes;
    size += sizeof(size_t);

    if (size < PAGE_SIZE) // Is it small enough that it'd be on our free list?
    {
        free_block* curr_block = free_list;
        free_block* parent_block = NULL;

        while (curr_block != NULL)
        {
            if (curr_block->size >= size) // We've found a big enough free block
            {
                long size_of_remainder = curr_block->size - size; // The amount of space left in the free block after we split it up
                
                // This statement effectively "removes" the recently allocated block from the free_list
                if (parent_block == NULL)
                {
                    free_list = curr_block->next;
                }
                else
                {
                    parent_block->next = curr_block->next;
                }

                if (size_of_remainder >= sizeof(free_block)) // Have we got enough space for another free_block?
                {
                    // We add back whatever wasn't allocated as a new block
                    void* address = (void*)curr_block + size;
                    insert_free_block(address, size_of_remainder);
                }
                else
                {
                    stats.chunks_allocated++;

                    curr_block->size = size - sizeof(size_t) + size_of_remainder;
                    pthread_mutex_unlock(&lock);
                    return (void*)curr_block + sizeof(size_t);
                }

                stats.chunks_allocated++;

                // Return the allocated block
                curr_block->size = size - sizeof(size_t);
                pthread_mutex_unlock(&lock);
                return (void*)curr_block + sizeof(size_t);
            }
            curr_block = curr_block->next;
        }

        void* page = mmap(
            NULL,
            PAGE_SIZE,
            PROT_READ|PROT_WRITE,
            MAP_ANON|MAP_PRIVATE,
            0, 0);
        stats.pages_mapped++;

        insert_free_block(page, PAGE_SIZE - sizeof(long));

        pthread_mutex_unlock(&lock);
        return xmalloc(size - sizeof(size_t));
    }
    else
    {
        stats.chunks_allocated++;
        long pages_needed = (long)div_up(size, PAGE_SIZE);
        
        free_block* ptr = mmap(
            NULL,
            pages_needed * PAGE_SIZE,
            PROT_READ|PROT_WRITE,
            MAP_ANON|MAP_PRIVATE,
            0, 0);
        stats.pages_mapped += pages_needed;

        ptr->size = size - sizeof(size_t);
        pthread_mutex_unlock(&lock);
        return (void*)ptr + sizeof(size_t);
    }
}

void
xfree(void* ptr)
{
    pthread_mutex_lock(&lock);
    stats.chunks_freed += 1;
    free_block* block = (free_block*)(ptr - sizeof(size_t));

    if (block->size < PAGE_SIZE)
    {
        insert_free_block(block, block->size);
    }
    else
    {
        stats.pages_unmapped += (long)div_up(block->size + sizeof(size_t), PAGE_SIZE);
        munmap(block, block->size);
    }
    pthread_mutex_unlock(&lock);
}

free_block*
xrealloc_extend(free_block* prev, size_t bytes)
{
    void* address = (void*)prev + bytes + sizeof(size_t);

    free_block* curr_block = free_list;
    free_block* parent = NULL;

    while (curr_block->next != NULL && (void*)curr_block <= address)
    {
        if ((void*)curr_block == address) // We're at the desired address
        {
            long needed_size = bytes - prev->size;
            if (needed_size <= curr_block->size) // The size of our current block is greater than equal what we need
            {
                // Remove the block from our free_list
                if (parent == NULL)
                {
                    free_list = curr_block->next;
                }
                else
                {
                    parent->next = curr_block->next;
                }

                prev->size = prev->size + curr_block->size;

                void* address = (void*)curr_block + needed_size;
                insert_free_block(address, curr_block->size - needed_size); // TODO check this for correctness

                return prev;
            }
            else // We've arrived at the address, but the size is insufficient. Might as well break and save some constant runtime
            {
                break;
            }
        }
        parent = curr_block;
        curr_block = curr_block->next;
    }
    return NULL;
}

void*
xrealloc_new(free_block* prev, size_t bytes)
{
    free_block* new_mem = (free_block*)(xmalloc(bytes) - sizeof(size_t));
    *new_mem = *prev;
    new_mem->size = bytes;
    xfree(prev);

    return (void*)new_mem + sizeof(size_t);
}

void*
xrealloc(void* prev, size_t bytes)
{
    free_block* old = (free_block*)(prev - sizeof(size_t));
    void* after = xrealloc_extend(old, bytes);

    if (after != NULL) {
        return (void*)after + sizeof(size_t);
    }
    else
    {
        return xrealloc_new(old, bytes);
    }

    return 0;
}