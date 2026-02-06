#include "arena.h"
#include <stdlib.h>
#include <string.h>

// Platform-specific virtual memory allocation
#ifdef _WIN32
#include <windows.h>
#define vm_alloc(size) VirtualAlloc(NULL, (size), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
#define vm_free(ptr, size) VirtualFree((ptr), 0, MEM_RELEASE)
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#define vm_alloc(size) ({ \
  void *_p = mmap(NULL, (size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); \
  _p == MAP_FAILED ? NULL : _p; \
})
#define vm_free(ptr, size) munmap((ptr), (size))
#else
// Fallback to malloc/free
#define vm_alloc(size) malloc(size)
#define vm_free(ptr, size) free(ptr)
#endif

// Allocate a new arena block and prepend it to the allocator's chain.
// Returns the new block, or NULL on failure.
static arena_t *arena_new_block(arena_allocator_t *allocator, size_t min_size) {
  size_t block_size = allocator->next_block_size;
  if (min_size > block_size)
    block_size = min_size;

  size_t total_size = sizeof(arena_t) + block_size;
  arena_t *block = (arena_t *)vm_alloc(total_size);
  if (!block)
    return NULL;

  block->buffer = (char *)(block + 1);
  block->size = block_size;
  block->used = 0;
  block->next = allocator->current;

  allocator->current = block;

  // Exponential growth: double for next allocation
  allocator->next_block_size = block_size * 2;

  return block;
}

arena_allocator_t *arena_create(size_t default_block_size) {
  // Allocate the allocator struct itself via vm_alloc
  arena_allocator_t *allocator =
      (arena_allocator_t *)vm_alloc(sizeof(arena_allocator_t));
  if (!allocator)
    return NULL;

  allocator->current = NULL;
  allocator->default_block_size = default_block_size;
  allocator->next_block_size = default_block_size;

  // Pre-allocate first block for cold-start optimization
  if (!arena_new_block(allocator, default_block_size)) {
    vm_free(allocator, sizeof(arena_allocator_t));
    return NULL;
  }

  return allocator;
}

void *arena_alloc(arena_allocator_t *allocator, size_t size) {
  // Align to 8 bytes
  size = (size + 7) & ~(size_t)7;

  arena_t *arena = allocator->current;

  if (!arena || arena->used + size > arena->size) {
    arena = arena_new_block(allocator, size);
    if (!arena)
      return NULL;
  }

  void *ptr = arena->buffer + arena->used;
  arena->used += size;
  return ptr;
}

void *arena_calloc(arena_allocator_t *allocator, size_t count,
                   size_t elem_size) {
  size_t total = count * elem_size;
  void *ptr = arena_alloc(allocator, total);
  if (ptr)
    memset(ptr, 0, total);
  return ptr;
}

char *arena_strdup(arena_allocator_t *allocator, const char *str) {
  if (!str)
    return NULL;
  size_t len = strlen(str) + 1;
  char *dup = (char *)arena_alloc(allocator, len);
  if (dup)
    memcpy(dup, str, len);
  return dup;
}

void *arena_realloc(arena_allocator_t *allocator, void *old_ptr,
                    size_t old_size, size_t new_size) {
  void *new_ptr = arena_alloc(allocator, new_size);
  if (new_ptr && old_ptr && old_size > 0) {
    memcpy(new_ptr, old_ptr, old_size < new_size ? old_size : new_size);
  }
  return new_ptr;
}

void arena_reset(arena_allocator_t *allocator) {
  // Reset all blocks for reuse (keep memory allocated)
  arena_t *arena = allocator->current;
  while (arena) {
    arena->used = 0;
    arena = arena->next;
  }
  // Reset growth back to default
  allocator->next_block_size = allocator->default_block_size;
}

void arena_destroy(arena_allocator_t *allocator) {
  if (!allocator)
    return;

  arena_t *arena = allocator->current;
  while (arena) {
    arena_t *next = arena->next;
    vm_free(arena, sizeof(arena_t) + arena->size);
    arena = next;
  }
  vm_free(allocator, sizeof(arena_allocator_t));
}
