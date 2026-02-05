#ifndef ARENA_H
#define ARENA_H

#include <Windows.h>

// Arena allocator for efficient memory management (no fragmentation, batch free)
//
// Benefits:
// - O(1) allocation (bump pointer)
// - O(1) bulk deallocation (free entire arena at once)
// - Zero fragmentation (allocations from contiguous blocks)
// - No individual free() calls needed
//
// Usage:
//   arena_allocator_t* arena = arena_create(65536);  // 64KB default block
//   void* ptr = arena_alloc(arena, size);            // Fast allocation
//   arena_reset(arena);                               // Reuse memory (optional)
//   arena_destroy(arena);                             // Free everything at once

typedef struct arena_t {
    char* buffer;
    SIZE_T size;
    SIZE_T used;
    struct arena_t* next;
} arena_t;

typedef struct {
    arena_t* current;
    SIZE_T default_block_size;
} arena_allocator_t;

// Create a new arena allocator with specified default block size
arena_allocator_t* arena_create(SIZE_T default_block_size);

// Allocate memory from arena (O(1), 8-byte aligned)
void* arena_alloc(arena_allocator_t* allocator, SIZE_T size);

// Reset arena for reuse (keeps memory allocated, resets pointers)
void arena_reset(arena_allocator_t* allocator);

// Destroy arena and free all memory (O(1) bulk free)
void arena_destroy(arena_allocator_t* allocator);

#endif  // ARENA_H
