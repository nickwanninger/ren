#pragma once


#include <atomic>
#include <mutex>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

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
      std::atomic<size_t> used;
      size_t size;
      char data[0];

      void* allocate(size_t size) {
        // Lock-free allocation using compare-and-swap
        size_t old_used = used.load(std::memory_order_relaxed);
        while (true) {
          if (old_used + size > this->size) { return nullptr; }
          if (used.compare_exchange_weak(old_used, old_used + size, std::memory_order_release,
                                         std::memory_order_relaxed)) {
            return &data[old_used];
          }
        }
      }

      size_t clear() {
        size_t old = used.exchange(0);
        // Return the absolute number of bytes taken up by this block (previously)
        return old + sizeof(Block);
      }
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

      // Free all blocks (no need for mutex as destructor is single-threaded)
      Block* block = m_current_block.load(std::memory_order_acquire);
      while (block) {
        Block* next = block->next;
        ::free(block);
        block = next;
      }
    }


    void* pushBytes(size_t size, bool zero = false) {
      Block* current = m_current_block.load(std::memory_order_acquire);

      if (current == nullptr) { current = new_block(size); }

      // Try to allocate from current block (lock-free)
      void* ptr = current->allocate(size);
      if (ptr == nullptr) {
        if (m_can_grow) {
          // Block is full, need a new one
          current = new_block(size);
          ptr = current->allocate(size);
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
        T* p = (T*)pushBytes(sizeof(T));
        ::new (p) T(std::forward<Args>(args)...);
        return p;
      } else {
        auto* node = (DtorNode*)pushBytes(sizeof(DtorNode) + sizeof(T));
        T* p = (T*)node->data;
        ::new (p) T(std::forward<Args>(args)...);
        node->dtor = [](void* obj) { static_cast<T*>(obj)->~T(); };

        // Lock-free prepend to destructor list using CAS
        DtorNode* old_head = m_dtor_list.load(std::memory_order_relaxed);
        do {
          node->next = old_head;
        } while (!m_dtor_list.compare_exchange_weak(old_head, node, std::memory_order_release,
                                                    std::memory_order_relaxed));
        return p;
      }
    }

    template <typename T>
    inline T* pushArray(size_t count) {
      auto p = (T*)pushBytes(sizeof(T) * count);
      for (size_t i = 0; i < count; i++) {
        ::new (&p[i]) T();
      }
      return p;
    }

    inline void disable_growth(void) { m_can_grow = false; }


    // Clear the arena, running destructors and resetting all blocks, and return
    // the number of bytes which were used.
    size_t clear(void) {
      std::lock_guard<std::mutex> lock(m_block_mutex);
      size_t size = 0;

      // Run destructors
      DtorNode* dtor_node = m_dtor_list.load(std::memory_order_acquire);
      while (dtor_node) {
        dtor_node->dtor(dtor_node->data);
        dtor_node = dtor_node->next;
      }
      m_dtor_list.store(nullptr, std::memory_order_release);

      // Clear blocks (reset their used counters)
      Block* block = m_current_block.load(std::memory_order_acquire);
      while (block) {
        size += block->clear();
        block = block->next;
      }
      return size;
    }


   private:
    Block* new_block(size_t required_size) {
      std::lock_guard<std::mutex> lock(m_block_mutex);

      // Double-check that we still need a new block (another thread may have created one)
      Block* current = m_current_block.load(std::memory_order_acquire);
      if (current != nullptr) {
        void* ptr = current->allocate(required_size);
        if (ptr != nullptr) { return current; }
      }

      size_t block_size = m_arena_size;
      if (required_size + sizeof(Block) > block_size) {
        block_size = required_size + sizeof(Block);
      }
      Block* new_block = (Block*)::malloc(sizeof(Block) + block_size);
      new_block->next = nullptr;
      new_block->used.store(0, std::memory_order_relaxed);
      new_block->size = block_size;

      // Link new block into the chain
      if (current) { current->next = new_block; }

      m_current_block.store(new_block, std::memory_order_release);
      return new_block;
    }

    std::atomic<Block*> m_current_block = nullptr;
    std::atomic<DtorNode*> m_dtor_list = nullptr;
    mutable std::mutex m_block_mutex;  // Protects block creation and clear operations

    size_t m_arena_size = 0;
    bool m_can_grow = 0;
  };
}  // namespace ren