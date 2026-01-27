#include "arena.h"

arena_allocator_t* arena_create(SIZE_T default_block_size) {
    arena_allocator_t* allocator = (arena_allocator_t*) VirtualAlloc(
            NULL, sizeof(arena_allocator_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!allocator) return NULL;

    allocator->default_block_size = default_block_size;
    allocator->current = NULL;

    return allocator;
}

void* arena_alloc(arena_allocator_t* allocator, SIZE_T size) {
    // Align to 8 bytes for better performance
    size = (size + 7) & ~7;

    arena_t* arena = allocator->current;

    // Check if current arena has enough space
    if (!arena || arena->used + size > arena->size) {
        // Allocate new arena block
        SIZE_T block_size =
                (size > allocator->default_block_size) ? size : allocator->default_block_size;
        SIZE_T total_size = sizeof(arena_t) + block_size;

        arena_t* new_arena =
                (arena_t*) VirtualAlloc(NULL, total_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (!new_arena) return NULL;

        new_arena->buffer = (char*) (new_arena + 1);
        new_arena->size = block_size;
        new_arena->used = 0;
        new_arena->next = allocator->current;

        allocator->current = new_arena;
        arena = new_arena;
    }

    void* ptr = arena->buffer + arena->used;
    arena->used += size;
    return ptr;
}

void arena_reset(arena_allocator_t* allocator) {
    // Reset all arenas for reuse (don't free memory - reuse it)
    arena_t* arena = allocator->current;
    while (arena) {
        arena->used = 0;
        arena = arena->next;
    }
}

void arena_destroy(arena_allocator_t* allocator) {
    // Free all arena blocks
    arena_t* arena = allocator->current;
    while (arena) {
        arena_t* next = arena->next;
        VirtualFree(arena, 0, MEM_RELEASE);
        arena = next;
    }
    VirtualFree(allocator, 0, MEM_RELEASE);
}
