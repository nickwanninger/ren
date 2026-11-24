#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
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

    // Push task to this worker's queue. Only the thread of this worker may
    // call it (ClDeque allows one owner).
    void push(ThreadPoolTask* task);

    ThreadPoolTask* steal();
    void join();
    u32 get_id() const { return m_id; }
    ThreadPool* get_pool() const { return m_pool; }
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

    // Parallel for loop: executes fn(i) for i in [0, count) in parallel.
    // Each task pulls chunks of indices from a shared counter until no
    // indices remain, so fast tasks take more of the work.
    template <typename Fn>
    void parallel_for(int count, Fn&& fn) {
      if (count <= 0) {
        return;
      }

      constexpr int chunk_size = 1;
      int num_chunks = (count + chunk_size - 1) / chunk_size;
      // int num_tasks = std::min<int>(get_num_workers(), num_chunks);
      int num_tasks = get_num_workers();

      std::atomic<int> next_index{0};
      std::atomic<int> counter{0};

      // The tasks hold next_index and fn by reference. This is safe because
      // this function waits for all tasks before it returns.
      for (int t = 0; t < num_tasks; ++t) {
        enqueue(
            [count, &next_index, &fn]() {
              while (true) {
                int start = next_index.fetch_add(chunk_size, std::memory_order_relaxed);
                if (start >= count) {
                  break;
                }

                int end = std::min(start + chunk_size, count);
                for (int i = start; i < end; ++i) {
                  fn(i);
                }
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
    ThreadPoolTask* pop_injected();
    // Block the calling worker until a task is queued or the pool shuts down.
    void wait_for_work();

    std::vector<Box<ThreadWorker>> m_workers;  // Each owns thread + queue
    std::atomic<bool> m_shutdown{false};

    // Tasks from threads outside the pool. Only the owner may push to a
    // worker's deque, so these go through this shared queue.
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<ThreadPoolTask*> m_injected;  // Guarded by m_mutex.
    // Tasks that are queued and not yet started. Workers sleep when it is 0.
    std::atomic<i64> m_pending{0};
    // Workers that are in wait_for_work().
    std::atomic<u32> m_sleepers{0};
  };


  ThreadPool& getThreadPool();

  template <typename Fn>
  void parallel_for(int count, Fn&& fn) {
    REN_PROFILE_SCOPE("parallel_for");
    getThreadPool().parallel_for(count, std::forward<Fn>(fn));
  }


  void backgroundWork(std::function<void(void)> fn);

}  // namespace ren
