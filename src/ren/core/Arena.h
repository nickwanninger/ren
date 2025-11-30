#pragma once


#include <stdlib.h>
#include <string.h>

namespace ren {

  // An arena is a memory buffer that can only be allocated to, and all the memory is released when
  // the arena is destructed. You can also optionally clear it without destructing it, but this will
  // not free the memory. This is useful for when you want to reuse the arena for a different
  // purpose without having to reallocate the memory.
  // It is constructed with a "size" and a boolean on if it can grow or not. If an arena grows, we
  // simply chain the blocks together.
  class Arena {
    struct Block {
      Block* next;
      size_t used;
      size_t size;
      char data[0];

      void* allocate(size_t size) {
        if (used + size > this->size) { return nullptr; }
        void* ptr = &data[used];
        used += size;
        return ptr;
      }

      void clear() { used = 0; }
    };

    struct DtorNode {
      DtorNode* next;
      void (*dtor)(void*);
      char data[0];
    };

   public:
    static const size_t DEFAULT_ARENA_SIZE = 4096 * 512;
    Arena(size_t arena_size = DEFAULT_ARENA_SIZE, bool can_grow = true)
        : m_arena_size(arena_size)
        , m_can_grow(can_grow) {}

    ~Arena() {
      clear();

      Block* block = m_current_block;
      while (block) {
        Block* next = block->next;
        ::free(block);
        block = next;
      }
    }


    void* push(size_t size, bool zero = false) {
      if (m_current_block == nullptr) { this->new_block(size); }

      // TODO: edge case!
      // if (size > (m_arena_size - sizeof(Block))) {
      //   return nullptr;
      // }
      void* ptr = m_current_block->allocate(size);
      if (ptr == nullptr) {
        if (m_can_grow) {
          new_block(size);
          ptr = m_current_block->allocate(size);
        } else {
          return nullptr;
        }
      }
      // unlikely
      if (zero) memset(ptr, 0, size);
      return ptr;
    }

    template <typename T, typename... Args>
    inline T* push(Args&&... args) {
      if constexpr (std::is_trivially_destructible<T>::value) {
        // no destructor needed
        T* p = (T*)push(sizeof(T));
        ::new (p) T(std::forward<Args>(args)...);
        return p;
      } else {
        auto* node = (DtorNode*)push(sizeof(DtorNode) + sizeof(T));
        T* p = (T*)node->data;
        ::new (p) T(std::forward<Args>(args)...);
        node->dtor = [](void* obj) { static_cast<T*>(obj)->~T(); };
        node->next = m_dtor_list;
        m_dtor_list = node;
        return p;
      }
    }

    template <typename T>
    inline T* pushArray(size_t count) {
      auto p = (T*)push(sizeof(T) * count);
      for (size_t i = 0; i < count; i++) {
        ::new (&p[i]) T();
      }
      return p;
    }


    inline void disable_growth(void) { m_can_grow = false; }
    inline size_t remaining(void) const { return m_current_block->size - m_current_block->used; }

    void clear(void) {
      // Run destructors
      while (m_dtor_list) {
        DtorNode* node = m_dtor_list;
        node->dtor(node->data);
        m_dtor_list = node->next;
      }

      // Clear blocks
      Block* block = m_current_block;
      while (block) {
        block->clear();
        block = block->next;
      }
    }


   private:
    Block* new_block(size_t required_size) {
      size_t block_size = m_arena_size;
      if (required_size + sizeof(Block) > block_size) {
        block_size = required_size + sizeof(Block);
      }
      Block* new_block = (Block*)::malloc(sizeof(Block) + block_size);
      new_block->next = nullptr;
      new_block->used = 0;
      new_block->size = block_size;
      if (m_current_block) { m_current_block->next = new_block; }
      m_current_block = new_block;
      return new_block;
    }

    Block* m_current_block = nullptr;
    DtorNode* m_dtor_list = nullptr;

    size_t m_arena_size = 0;
    bool m_can_grow = 0;
  };
}  // namespace ren