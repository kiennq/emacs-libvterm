#include "arena.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define ARENA_ALLOC(size)                                                      \
  VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
#define ARENA_FREE(ptr) VirtualFree(ptr, 0, MEM_RELEASE)
#else
#include <sys/mman.h>
#define ARENA_ALLOC(size)                                                      \
  mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
#define ARENA_FREE(ptr)                                                        \
  munmap(ptr, 0) /* Note: munmap needs size, handled specially */
#endif

/* Maximum block size cap (16MB) */
#define ARENA_MAX_BLOCK_SIZE (16 * 1024 * 1024)

arena_allocator_t *arena_create(size_t default_block_size) {
#ifdef _WIN32
  arena_allocator_t *allocator = (arena_allocator_t *)VirtualAlloc(
      NULL, sizeof(arena_allocator_t), MEM_COMMIT | MEM_RESERVE,
      PAGE_READWRITE);
#else
  arena_allocator_t *allocator =
      (arena_allocator_t *)malloc(sizeof(arena_allocator_t));
#endif

  if (!allocator)
    return NULL;

  allocator->default_block_size = default_block_size;
  allocator->last_block_size = default_block_size;
  allocator->current = NULL;

  return allocator;
}

void *arena_alloc(arena_allocator_t *allocator, size_t size) {
  /* Align to 8 bytes for better performance */
  size = (size + 7) & ~((size_t)7);

  arena_t *arena = allocator->current;

  /* Check if current arena has enough space */
  if (!arena || arena->used + size > arena->size) {
    /* Exponential growth: double the last block size
     * But ensure it's at least as big as the requested size */
    size_t next_size = allocator->last_block_size * 2;
    size_t block_size = (size > next_size) ? size : next_size;

    /* Cap at reasonable maximum to prevent runaway growth */
    if (block_size > ARENA_MAX_BLOCK_SIZE) {
      block_size = (size > ARENA_MAX_BLOCK_SIZE) ? size : ARENA_MAX_BLOCK_SIZE;
    }

    size_t total_size = sizeof(arena_t) + block_size;

#ifdef _WIN32
    arena_t *new_arena = (arena_t *)VirtualAlloc(
        NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    arena_t *new_arena = (arena_t *)malloc(total_size);
#endif

    if (!new_arena)
      return NULL;

    new_arena->buffer = (char *)(new_arena + 1);
    new_arena->size = block_size;
    new_arena->used = 0;
    new_arena->next = allocator->current;

    allocator->current = new_arena;
    allocator->last_block_size = block_size;
    arena = new_arena;
  }

  void *ptr = arena->buffer + arena->used;
  arena->used += size;
  return ptr;
}

void arena_reset(arena_allocator_t *allocator) {
  /* Reset all arenas for reuse (don't free memory - reuse it) */
  arena_t *arena = allocator->current;
  while (arena) {
    arena->used = 0;
    arena = arena->next;
  }
  /* Reset exponential growth tracking to default
   * (existing blocks are still available for reuse) */
  allocator->last_block_size = allocator->default_block_size;
}

void arena_destroy(arena_allocator_t *allocator) {
  /* Free all arena blocks */
  arena_t *arena = allocator->current;
  while (arena) {
    arena_t *next = arena->next;
#ifdef _WIN32
    VirtualFree(arena, 0, MEM_RELEASE);
#else
    free(arena);
#endif
    arena = next;
  }
#ifdef _WIN32
  VirtualFree(allocator, 0, MEM_RELEASE);
#else
  free(allocator);
#endif
}
