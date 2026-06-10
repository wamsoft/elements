/*=============================================================================
   Copyright (c) 2026 Cycfi Research

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/support/task_queue.hpp>
#include <algorithm>
#include <iterator>
#include <utility>

namespace cycfi::elements::detail
{
   void task_queue::post(task_fn f)
   {
      std::lock_guard lk{_mtx};
      if (_stopped)
         return;
      _ready.push_back(std::move(f));
   }

   task_queue::timer_ptr
   task_queue::post_after(clock::duration d, task_fn f)
   {
      auto handle = std::make_shared<timer_handle>();
      std::lock_guard lk{_mtx};
      if (_stopped)
         return handle;
      _timers.push_back({clock::now() + d, std::move(f), handle});
      return handle;
   }

   void task_queue::poll()
   {
      // Take a snapshot of work due *now*, then run it with the lock
      // released. Tasks posted from inside a callback land in the queue
      // and wait for the next poll() — no recursion, no surprise
      // ordering.
      std::deque<task_fn> ready_now;
      std::vector<timer_entry> fired;
      auto const now = clock::now();
      {
         std::lock_guard lk{_mtx};
         ready_now.swap(_ready);

         // Partition expired timers to the end, then move them out.
         auto it = std::partition(
            _timers.begin(), _timers.end(),
            [now](timer_entry const& e) { return e.at > now; }
         );
         fired.assign(
            std::make_move_iterator(it),
            std::make_move_iterator(_timers.end())
         );
         _timers.erase(it, _timers.end());
      }

      for (auto& f : ready_now)
         f();

      for (auto& e : fired)
      {
         if (e.handle && e.handle->alive.load(std::memory_order_relaxed))
            e.fn();
      }
   }

   void task_queue::stop()
   {
      std::lock_guard lk{_mtx};
      _stopped = true;
      _ready.clear();
      _timers.clear();
   }
}
