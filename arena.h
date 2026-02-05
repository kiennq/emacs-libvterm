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
// - Exponential block growth (2x) reduces allocation frequency
//
// Usage:
//   arena_allocator_t* arena = arena_create(65536);  // 64KB initial block
//   void* ptr = arena_alloc(arena, size);            // Fast allocation
//   arena_reset(arena);                              // Reuse memory (optional)
//   arena_destroy(arena);                            // Free everything at once
//
// Growth strategy:
//   - First block: default_block_size (e.g., 64KB)
//   - Second block: 2x previous (128KB)
//   - Third block: 2x previous (256KB)
//   - Capped at 16MB to prevent runaway growth
//   - arena_reset() resets growth tracking to default

typedef struct arena_t {
  char *buffer;
  size_t size;
  size_t used;
  struct arena_t *next;
} arena_t;

typedef struct {
  arena_t *current;
  size_t default_block_size;
  size_t last_block_size; /* For exponential growth: each new block is 2x */
} arena_allocator_t;

// Create a new arena allocator with specified default block size
arena_allocator_t *arena_create(size_t default_block_size);

// Allocate memory from arena (O(1), 8-byte aligned)
void *arena_alloc(arena_allocator_t *allocator, size_t size);

// Reset arena for reuse (keeps memory allocated, resets pointers)
void arena_reset(arena_allocator_t *allocator);

// Destroy arena and free all memory (O(1) bulk free)
void arena_destroy(arena_allocator_t *allocator);

#endif // ARENA_H
