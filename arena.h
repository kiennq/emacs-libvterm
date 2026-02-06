#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

// Arena allocator for efficient memory management (no fragmentation, batch
// free)
//
// Benefits:
// - O(1) allocation (bump pointer)
// - O(1) bulk deallocation (free entire arena at once)
// - Zero fragmentation (allocations from contiguous blocks)
// - No individual free() calls needed
// - Exponential block growth (reduces syscalls for growing workloads)
//
// Usage:
//   arena_allocator_t* arena = arena_create(65536);  // 64KB initial block
//   void* ptr = arena_alloc(arena, size);            // Fast allocation
//   arena_reset(arena);                               // Reuse memory
//   (optional) arena_destroy(arena);                             // Free
//   everything at once

typedef struct arena_t {
  char *buffer;
  size_t size;
  size_t used;
  struct arena_t *next;
} arena_t;

typedef struct {
  arena_t *current;
  size_t default_block_size;
  size_t next_block_size; // Exponential growth: doubles each time
} arena_allocator_t;

// Create a new arena allocator with specified initial block size
arena_allocator_t *arena_create(size_t initial_block_size);

// Allocate memory from arena (O(1), 8-byte aligned)
void *arena_alloc(arena_allocator_t *allocator, size_t size);

// Allocate zeroed memory from arena
void *arena_calloc(arena_allocator_t *allocator, size_t count,
                   size_t elem_size);

// Duplicate a string into the arena
char *arena_strdup(arena_allocator_t *arena, const char *str);

// Reallocate memory from arena (allocates new, copies old, abandons old
// pointer)
void *arena_realloc(arena_allocator_t *allocator, void *old_ptr,
                    size_t old_size, size_t new_size);

// Reset arena for reuse (keeps memory allocated, resets pointers)
void arena_reset(arena_allocator_t *allocator);

// Destroy arena and free all memory (O(1) bulk free)
void arena_destroy(arena_allocator_t *allocator);

#endif // ARENA_H
