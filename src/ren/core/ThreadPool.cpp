#include <ren/core/ThreadPool.h>
#include <ren/core/Flag.h>
#include <ren/core/Logging.h>
#include <cstdlib>
#include <random>

namespace ren {

  inline Flag<int> g_thread_pool_workers("thread-pool-workers", -1, "Number of worker threads (-1 = auto-detect from hardware_concurrency)");

  thread_local ThreadWorker* t_current_worker = nullptr;
  thread_local std::mt19937 t_rng(std::random_device{}());

  ThreadWorker::ThreadWorker(ThreadPool* pool, u32 id)
      : m_pool(pool)
      , m_id(id) {
    m_thread = std::thread(&ThreadWorker::worker_loop, this);
  }

  ThreadWorker::~ThreadWorker() {}

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

  bool ThreadWorker::schedule_one() {
    ThreadPoolTask* task = nullptr;

    bool success = false;
    task = m_queue.pop(&success);

    // If local queue empty, try to steal from others (FIFO)
    if (!success || !task) {
      task = m_pool->try_steal(this);
      if (task != nullptr) {
        // Stolen tasks are executed in FIFO order
        REN_PROFILE_MARK("StoleTask");
      }
    }

    if (task) {
      // Execute task
      // fmt::println("Executing task on worker {}", m_id);
      task->execute();

      // Decrement completion counter if present
      if (task->m_completion_counter) {
        task->m_completion_counter->fetch_sub(1, std::memory_order_release);
      }

      delete task;  // Clean up allocated task
      return true;
    }

    return false;
  }

  void ThreadWorker::worker_loop() {
    REN_PROFILE_SCOPE("ThreadWorker::loop");

    t_current_worker = this;

    while (!m_pool->is_shutdown()) {
      if (!schedule_one()) {
        // std::this_thread::yield();
      }
    }
  }


  ThreadPool::ThreadPool() {
    int flag_value = g_thread_pool_workers.get();

    u32 num_workers = 0;
    if (flag_value < 0) {
      num_workers = std::thread::hardware_concurrency();
      if (num_workers == 0) {
        num_workers = 4;  // Fallback
      }
    } else if (flag_value == 0) {
      num_workers = 1;  // Single-threaded mode
    } else {
      num_workers = static_cast<u32>(flag_value);
    }

    ren::dbgln("ThreadPool: Creating {} worker threads", num_workers);

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
    u32 num_workers = static_cast<u32>(m_workers.size());
    if (num_workers == 1) {
      m_workers[0]->push(task);
      return;
    }
    // Pick two random workers and choose the less loaded one
    std::uniform_int_distribution<u32> dist(0, num_workers - 1);
    u32 n = dist(t_rng);
    u32 n_plus_1 = (n + 1) % num_workers;

    // Compare queue sizes and push to the less loaded worker
    if (m_workers[n]->queue_size() <= m_workers[n_plus_1]->queue_size()) {
      m_workers[n]->push(task);
    } else {
      m_workers[n_plus_1]->push(task);
    }
  }

  ThreadPoolTask* ThreadPool::try_steal(ThreadWorker* current_worker) {
    u32 num_workers = static_cast<u32>(m_workers.size());
    if (num_workers <= 1) {
      return nullptr;
    }

    // Try to steal from random workers
    static std::uniform_int_distribution<u32> dist(0, num_workers - 2);
    for (u32 i = 0; i < num_workers; ++i) {
      // Pick random victim (avoid self)
      u32 offset = dist(t_rng);
      u32 victim_idx = (current_worker->get_id() + 1 + offset) % num_workers;

      auto* task = m_workers[victim_idx]->steal();
      if (task) {
        return task;
      }
    }

    return nullptr;
  }

  void ThreadPool::wait_for_completion(std::atomic<int>* counter) {
    REN_PROFILE_SCOPE("ThreadPool::wait_for_completion");

    bool is_worker = (t_current_worker != nullptr);
    int initial_count = counter->load(std::memory_order_acquire);

    ren::dbgln("wait_for_completion: initial count = {}, is_worker = {}", initial_count, is_worker);

    while (counter->load(std::memory_order_acquire) > 0) {
      // If we're on a worker thread, help complete work to prevent deadlock
      if (t_current_worker != nullptr) {
        if (!t_current_worker->schedule_one()) {
          // No work available, yield
          std::this_thread::yield();
        }
      } else {
        // External thread (not a worker), just yield
        std::this_thread::yield();
      }
    }

    ren::dbgln("wait_for_completion: complete");
  }


  ThreadPool& getThreadPool() {
    static ThreadPool instance;
    return instance;
  }

}  // namespace ren
