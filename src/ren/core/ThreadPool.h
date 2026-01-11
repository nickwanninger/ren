#pragma once

#include <atomic>
#include <thread>
#include <ren/core/ClDeque.h>
#include <ren/types.h>

namespace ren {

class ThreadPool;
class ThreadWorker;

// Base class for all thread pool tasks
class ThreadPoolTask {
 public:
  virtual ~ThreadPoolTask() = default;

  // Execute the task
  virtual void execute() = 0;

  // Optional: completion counter
  std::atomic<int>* m_completion_counter = nullptr;
};

// Template implementation for callable types
template <typename F>
class LambdaTask : public ThreadPoolTask {
 public:
  explicit LambdaTask(F&& fn) : m_fn(std::forward<F>(fn)) {}

  void execute() override { m_fn(); }

 private:
  F m_fn;
};

// Worker thread with its own work queue
class ThreadWorker {
 public:
  ThreadWorker(ThreadPool* pool, u32 id);
  ~ThreadWorker();

  // Push task to this worker's queue
  void push(ThreadPoolTask* task);

  // Try to steal a task from this worker
  ThreadPoolTask* steal();

  // Join the worker thread (called during shutdown)
  void join();

  // Worker ID (for work stealing)
  u32 get_id() const { return m_id; }

 private:
  void worker_loop();

  ThreadPool* m_pool;
  u32 m_id;
  ClDeque<ThreadPoolTask*> m_queue;
  std::thread m_thread;
};

// Thread pool for parallel task execution
class ThreadPool {
 public:
  // Constructor: creates worker threads based on flag
  ThreadPool();

  // Destructor: clean shutdown (wait for tasks, join threads)
  ~ThreadPool();

  // Submit a task (template for any callable)
  template <typename F>
  void enqueue(F&& fn) {
    auto* task = new LambdaTask<F>(std::forward<F>(fn));
    enqueue_task(task);
  }

  // Submit a task with completion tracking
  template <typename F>
  void enqueue(F&& fn, std::atomic<int>* counter) {
    auto* task = new LambdaTask<F>(std::forward<F>(fn));
    task->m_completion_counter = counter;
    counter->fetch_add(1, std::memory_order_relaxed);
    enqueue_task(task);
  }

  // Wait until counter reaches zero (spin with yields)
  void wait_for_completion(std::atomic<int>* counter);

  // Get number of worker threads
  u32 get_num_workers() const { return static_cast<u32>(m_workers.size()); }

  // Access to shutdown flag (for workers)
  bool is_shutdown() const { return m_shutdown.load(std::memory_order_relaxed); }

 private:
  friend class ThreadWorker;

  // Internal enqueue (non-template)
  void enqueue_task(ThreadPoolTask* task);

  // Try to steal work from other workers (called by ThreadWorker)
  ThreadPoolTask* try_steal(ThreadWorker* current_worker);

  // Data members
  std::vector<Box<ThreadWorker>> m_workers;  // Each owns thread + queue
  std::atomic<bool> m_shutdown{false};
  std::atomic<u32> m_next_worker{0};  // For round-robin task assignment
};

// RAII wrapper for completion tracking
class CompletionCounter {
 public:
  CompletionCounter() : m_counter(0) {}

  // Get pointer to counter for enqueue
  std::atomic<int>* get_counter() { return &m_counter; }

  // Wait for all tasks to complete
  void wait();

  // Get current count (for debugging)
  int get_count() const { return m_counter.load(std::memory_order_relaxed); }

 private:
  std::atomic<int> m_counter;
};

// Global thread pool instance (initialized on first use)
ThreadPool& get_thread_pool();

}  // namespace ren
