#pragma once

#include <atomic>
#include <thread>
#include "ren/core/Logging.h"
#include <ren/core/ClDeque.h>
#include <ren/types.h>

namespace ren {

  class ThreadPool;
  class ThreadWorker;

  // Base class for all thread pool tasks
  class ThreadPoolTask {
   public:
    virtual ~ThreadPoolTask() = default;
    virtual void execute() = 0;
    std::atomic<int>* m_completion_counter = nullptr;
  };


  template <typename F>
  class LambdaTask : public ThreadPoolTask {
   public:
    explicit LambdaTask(F&& fn)
        : m_fn(std::forward<F>(fn)) {}

    void execute() override { m_fn(); }

   private:
    F m_fn;
  };


  class ThreadWorker {
   public:
    ThreadWorker(ThreadPool* pool, u32 id);
    ~ThreadWorker();

    // Push task to this worker's queue
    void push(ThreadPoolTask* task);

    ThreadPoolTask* steal();
    void join();
    u32 get_id() const { return m_id; }
    int64_t queue_size() const { return m_queue.size(); }

    // Try to execute one task (from local queue or by stealing)
    // Returns true if a task was executed, false if no work available
    bool schedule_one();

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
    ThreadPool();
    ~ThreadPool();

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


    void wait_for_completion(std::atomic<int>* counter);

    // Parallel for loop: executes fn(i) for i in [0, count) in parallel
    // Work is divided into chunks for better cache locality and lower overhead
    template <typename Fn>
    void parallel_for(int count, Fn&& fn) {
      if (count <= 0) {
        return;
      }

      u32 num_workers = get_num_workers();
      int chunk_size = (count + num_workers - 1) / num_workers;
      if (chunk_size < 1) {
        chunk_size = 1;
      }

      std::atomic<int> counter{0};

      for (int start = 0; start < count; start += chunk_size) {
        int end = std::min(start + chunk_size, count);

        enqueue(
            [start, end, fn = std::forward<Fn>(fn)]() {
              for (int i = start; i < end; ++i) {
                fn(i);
              }
            },
            &counter);
      }

      wait_for_completion(&counter);
    }

    u32 get_num_workers() const { return static_cast<u32>(m_workers.size()); }
    bool is_shutdown() const { return m_shutdown.load(std::memory_order_relaxed); }

   private:
    friend class ThreadWorker;
    void enqueue_task(ThreadPoolTask* task);
    ThreadPoolTask* try_steal(ThreadWorker* current_worker);
    std::vector<Box<ThreadWorker>> m_workers;  // Each owns thread + queue
    std::atomic<bool> m_shutdown{false};
  };


  ThreadPool& getThreadPool();

  template <typename Fn>
  void parallel_for(int count, Fn&& fn) {
    getThreadPool().parallel_for(count, std::forward<Fn>(fn));
  }

}  // namespace ren
