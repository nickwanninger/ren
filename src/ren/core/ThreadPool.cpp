#include <ren/core/ThreadPool.h>
#include <ren/core/Flag.h>
#include <ren/core/Logging.h>
#include <cstdlib>

namespace ren {

// Global flag for thread pool configuration
inline Flag<int> g_thread_pool_workers("thread-pool-workers", -1,
                                        "Number of worker threads (-1 = auto-detect from hardware_concurrency)");

//=============================================================================
// ThreadWorker Implementation
//=============================================================================

ThreadWorker::ThreadWorker(ThreadPool* pool, u32 id) : m_pool(pool), m_id(id) {
  m_thread = std::thread(&ThreadWorker::worker_loop, this);
}

ThreadWorker::~ThreadWorker() {
  // Thread should already be joined by ThreadPool destructor
}

void ThreadWorker::push(ThreadPoolTask* task) { m_queue.push(task); }

ThreadPoolTask* ThreadWorker::steal() {
  bool success = false;
  auto result = m_queue.steal(&success);
  return success ? result : nullptr;
}

void ThreadWorker::join() {
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void ThreadWorker::worker_loop() {
  REN_PROFILE_SCOPE("ThreadWorker::loop");

  while (!m_pool->is_shutdown()) {
    ThreadPoolTask* task = nullptr;

    // Try to pop from local queue first (LIFO for cache locality)
    bool success = false;
    task = m_queue.pop(&success);

    // If local queue empty, try to steal from others (FIFO)
    if (!success || !task) {
      task = m_pool->try_steal(this);
    }

    if (task) {
      // Execute task
      task->execute();

      // Decrement completion counter if present
      if (task->m_completion_counter) {
        task->m_completion_counter->fetch_sub(1, std::memory_order_release);
      }

      delete task;  // Clean up allocated task
    } else {
      // No work available, yield to avoid busy-wait
      std::this_thread::yield();
    }
  }
}

//=============================================================================
// ThreadPool Implementation
//=============================================================================

ThreadPool::ThreadPool() {
  int flag_value = g_thread_pool_workers.get();

  u32 num_workers = 0;
  if (flag_value < 0) {
    num_workers = std::thread::hardware_concurrency();
    if (num_workers == 0) num_workers = 4;  // Fallback
  } else if (flag_value == 0) {
    num_workers = 1;  // Single-threaded mode
  } else {
    num_workers = static_cast<u32>(flag_value);
  }

  ren::dbgln("ThreadPool: Creating {} worker threads", num_workers);

  // Create workers (each creates its own thread and queue)
  m_workers.reserve(num_workers);
  for (u32 i = 0; i < num_workers; ++i) {
    m_workers.push_back(std::make_unique<ThreadWorker>(this, i));
  }
}

ThreadPool::~ThreadPool() {
  // Signal shutdown
  m_shutdown.store(true, std::memory_order_relaxed);

  // Join all worker threads
  for (auto& worker : m_workers) {
    worker->join();
  }
}

void ThreadPool::enqueue_task(ThreadPoolTask* task) {
  // Round-robin assignment to workers
  u32 target = m_next_worker.fetch_add(1, std::memory_order_relaxed) % m_workers.size();
  m_workers[target]->push(task);
}

ThreadPoolTask* ThreadPool::try_steal(ThreadWorker* current_worker) {
  u32 num_workers = static_cast<u32>(m_workers.size());
  if (num_workers <= 1) return nullptr;

  // Try to steal from random workers
  for (u32 i = 0; i < num_workers; ++i) {
    // Pick random victim (avoid self)
    u32 victim_idx = (current_worker->get_id() + 1 + (rand() % (num_workers - 1))) % num_workers;

    auto* task = m_workers[victim_idx]->steal();
    if (task) {
      return task;
    }
  }

  return nullptr;
}

void ThreadPool::wait_for_completion(std::atomic<int>* counter) {
  REN_PROFILE_SCOPE("ThreadPool::wait_for_completion");

  while (counter->load(std::memory_order_acquire) > 0) {
    // Spin with yields to avoid burning CPU
    std::this_thread::yield();
  }
}

//=============================================================================
// CompletionCounter Implementation
//=============================================================================

void CompletionCounter::wait() {
  get_thread_pool().wait_for_completion(&m_counter);
}

//=============================================================================
// Global ThreadPool Instance
//=============================================================================

ThreadPool& get_thread_pool() {
  static ThreadPool instance;
  return instance;
}

}  // namespace ren
