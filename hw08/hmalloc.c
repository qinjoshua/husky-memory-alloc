#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>

#include "hmalloc.h"

typedef struct free_block {
    long size;
    struct free_block* next;
} free_block;

/*typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
} hm_stats;*/

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static free_block* free_list = 0;

long
free_list_length()
{
    long length = 0;
    free_block* next_block = free_list;

    while (next_block != NULL)
    {
        length++;
        next_block = next_block->next;
    }

    return length;
}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
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
hmalloc(size_t size)
{
    size += sizeof(size_t);

    if (size < PAGE_SIZE) // Is it small enough that it'd be on our free list?
    {
        free_block* curr_block = free_list;
        free_block* parent_block = NULL;
        int successfully_set = 0;

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
                    insert_free_block(address, size_of_remainder - sizeof(size_t));
                }
                // else
                // {
                //     void* address = (void*)curr_block + size;
                //     if (address + size_of_remainder == (void*)curr_block->next)
                //     {
                //         insert_free_block(address, size_of_remainder - sizeof(size_t));
                //     }
                // }

                stats.chunks_allocated++;

                // Return the allocated block
                curr_block->size = size - sizeof(size_t);
                curr_block->next = NULL;
                return (void*)curr_block + sizeof(size_t);
            }
            curr_block = curr_block->next;
        }

        if (!successfully_set) // We'll reach here if the free list has no blocks big enough
        {
            void* page = mmap(
                NULL,
                PAGE_SIZE,
                PROT_READ|PROT_WRITE,
                MAP_ANON|MAP_PRIVATE,
                0, 0);
            stats.pages_mapped++;

            insert_free_block(page, PAGE_SIZE - sizeof(long));
            return hmalloc(size - sizeof(size_t));
        }
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
        return (void*)ptr + sizeof(size_t);
    }
}

void
hfree(void* item)
{
    stats.chunks_freed += 1;
    free_block* block = (free_block*)(item - sizeof(size_t));

    if (block->size < PAGE_SIZE)
    {
        insert_free_block(block, block->size);
    }
    else
    {
        stats.pages_unmapped += (long)div_up(block->size + sizeof(size_t), PAGE_SIZE);
        munmap(block, block->size);
    }
}

