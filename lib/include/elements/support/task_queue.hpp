/*=============================================================================
   Copyright (c) 2026 Cycfi Research

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]

   Minimal task queue used by view to defer work and run one-shot timers.
   Replaces Asio's io_context for the single use case it was carrying here:
   posting callables to the UI thread and scheduling delayed callbacks.

   Single-consumer, multi-producer:
      - post() / post_after() may be called from any thread.
      - poll() must only be called from the UI thread.
=============================================================================*/
#if !defined(ELEMENTS_TASK_QUEUE_2026)
#define ELEMENTS_TASK_QUEUE_2026

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace cycfi::elements::detail
{
   struct timer_handle
   {
      // poll() reads this; cancel() sets it. Relaxed is fine: a missed
      // cancel just means the callback runs one extra time, which is
      // already the contract for any post-deadline cancel.
      std::atomic<bool> alive{true};
      void cancel() { alive.store(false, std::memory_order_relaxed); }
   };

   class task_queue
   {
   public:
      using clock     = std::chrono::steady_clock;
      using task_fn   = std::function<void()>;
      using timer_ptr = std::shared_ptr<timer_handle>;

                  task_queue() = default;
                  task_queue(task_queue const&) = delete;
      task_queue& operator=(task_queue const&) = delete;

      void        post(task_fn f);
      timer_ptr   post_after(clock::duration d, task_fn f);

      // Run everything ready (expired timers + posted tasks). Tasks
      // posted from inside a task run on the *next* poll, never
      // re-entrantly inside this one.
      void        poll();

      // Drop all queued work and ignore future posts. Called by
      // ~view to guarantee no callback fires after destruction.
      void        stop();

   private:
      struct timer_entry
      {
         clock::time_point at;
         task_fn           fn;
         timer_ptr         handle;
      };

      std::mutex                _mtx;
      std::deque<task_fn>       _ready;
      std::vector<timer_entry>  _timers;
      bool                      _stopped = false;
   };
}

#endif
