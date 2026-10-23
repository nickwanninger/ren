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
    if (!success) {
      task = nullptr;
    }

    // Then the tasks from outside the pool.
    if (task == nullptr) {
      task = m_pool->pop_injected();
    }

    // If local queue empty, try to steal from others (FIFO)
    if (task == nullptr) {
      task = m_pool->try_steal(this);
      if (task != nullptr) {
        // Stolen tasks are executed in FIFO order
        REN_PROFILE_MARK("StoleTask");
      }
    }

    if (task) {
      m_pool->m_pending.fetch_sub(1, std::memory_order_seq_cst);
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
    t_current_worker = this;

    while (!m_pool->is_shutdown()) {
      if (!schedule_one()) {
        m_pool->wait_for_work();
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
    // Signal shutdown. Set it under the mutex, so that a worker cannot miss
    // the wake between its check and its wait.
    {
      std::lock_guard lock(m_mutex);
      m_shutdown.store(true, std::memory_order_relaxed);
    }
    m_wake.notify_all();

    // Join all worker threads
    for (auto& worker : m_workers) {
      worker->join();
    }

    for (auto* task : m_injected) {
      delete task;
    }
  }

  void ThreadPool::enqueue_task(ThreadPoolTask* task) {
    m_pending.fetch_add(1, std::memory_order_seq_cst);

    // A worker of this pool pushes to its own deque. All other threads use
    // the shared queue: ClDeque::push is not safe from a second thread.
    ThreadWorker* worker = t_current_worker;
    if (worker != nullptr && worker->get_pool() == this) {
      worker->push(task);
    } else {
      std::lock_guard lock(m_mutex);
      m_injected.push_back(task);
    }

    // m_pending and m_sleepers are both seq_cst, so either a sleeping worker
    // sees the new task, or this thread sees the sleeper. The lock makes sure
    // that the sleeper is in the wait before the notify.
    if (m_sleepers.load(std::memory_order_seq_cst) > 0) {
      { std::lock_guard lock(m_mutex); }
      m_wake.notify_one();
    }
  }

  ThreadPoolTask* ThreadPool::pop_injected() {
    std::lock_guard lock(m_mutex);
    if (m_injected.empty()) {
      return nullptr;
    }
    auto* task = m_injected.front();
    m_injected.pop_front();
    return task;
  }

  void ThreadPool::wait_for_work() {
    std::unique_lock lock(m_mutex);
    m_sleepers.fetch_add(1, std::memory_order_seq_cst);
    m_wake.wait(lock, [this] {
      return m_pending.load(std::memory_order_seq_cst) > 0 || m_shutdown.load(std::memory_order_relaxed);
    });
    m_sleepers.fetch_sub(1, std::memory_order_seq_cst);
  }

  ThreadPoolTask* ThreadPool::try_steal(ThreadWorker* current_worker) {
    u32 num_workers = static_cast<u32>(m_workers.size());
    if (num_workers <= 1) {
      return nullptr;
    }

    // Visit each other worker one time, from a random start.
    u32 offset = static_cast<u32>(t_rng() % num_workers);
    for (u32 i = 0; i < num_workers; ++i) {
      u32 victim_idx = (offset + i) % num_workers;
      if (victim_idx == current_worker->get_id()) {
        continue;
      }

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

    // ren::dbgln("wait_for_completion: initial count = {}, is_worker = {}", initial_count, is_worker);

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

    // ren::dbgln("wait_for_completion: complete");
  }


  ThreadPool& getThreadPool() {
    static ThreadPool instance;
    return instance;
  }


  void backgroundWork(std::function<void(void)> fn) {
    getThreadPool().enqueue(std::move(fn));
  }

}  // namespace ren
