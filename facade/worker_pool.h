#pragma once
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace facade
{
    namespace utils
    {
        template <typename t_function, typename... t_args>
        void thread_starter(
            const size_t thread_id, const t_function& function, t_args... args)
        {
            // add here anything needed for initializing thread context
            function(args...);
        }

        template <typename t_function, typename... t_args>
        std::thread make_thread(
            const size_t thread_id, const t_function& function, t_args... args)
        {
            return std::thread{
                thread_starter<t_function, t_args...>, thread_id, function, args...};
        }

        class worker_pool
        {
        public:
            using t_task = std::function<void()>;

        private:
            size_t m_workers_num{0};
            size_t m_current_workload{0};
            std::vector<std::thread> m_workers;
            std::queue<t_task> m_task_queue;
            mutable std::mutex m_queue_mutex;
            std::condition_variable m_cv;
            bool m_running{false};

            using t_task_queue = decltype(m_task_queue);
            using t_lock_guard = std::lock_guard<decltype(m_queue_mutex)>;
            using t_unique_lock = std::unique_lock<decltype(m_queue_mutex)>;

        private:
            std::pair<bool, t_task> get_next_task()
            {
                t_task task;
                {
                    t_unique_lock ulck(m_queue_mutex);
                    while (m_task_queue.empty() && m_running) { m_cv.wait(ulck); }

                    if (!m_running) return {false, t_task{}};

                    task = std::move(m_task_queue.front());
                    m_task_queue.pop();
                    m_current_workload++;
                }

                m_cv.notify_all();
                return {true, task};
            }

            void worker()
            {
                while (m_running) {
                    const auto [continue_execution, performer] = get_next_task();
                    if (!continue_execution) return;

                    try {
                        performer();
                    } catch (...) {
                        // TODO: decide what do we do in this case
                        // it's either stop or continue
                        continue;
                    }
                    {
                        t_lock_guard lg(m_queue_mutex);
                        m_current_workload--;
                    }
                    m_cv.notify_all();
                }
            }

            bool thread_belongs_to_pool() const
            {
                const auto this_thread_id = std::this_thread::get_id();
                for (const auto& thread : m_workers) {
                    if (this_thread_id == thread.get_id()) return true;
                }

                return false;
            }

            auto wait_completion_and_get_lock()
            {
                t_unique_lock ulck(m_queue_mutex);
                while (!m_task_queue.empty() && m_running || m_current_workload != 0) {
                    m_cv.wait(ulck);
                }
                return std::move(ulck);
            }

        public:
            void wait_completion() { wait_completion_and_get_lock(); }

            void stop()
            {
                {
                    const auto lock = wait_completion_and_get_lock();
                    m_running = false;
                    m_cv.notify_all();
                }
                for (auto& thread : m_workers) { thread.join(); }
                m_current_workload = 0;
                m_workers.clear();
            }

            void start()
            {
                if (m_running) return;

                m_running = true;
                for (size_t idx = 0; idx < m_workers_num; ++idx) {
                    const auto binder = [this]() { worker(); };
                    m_workers.emplace_back(make_thread(idx, binder));
                }
            }

            template <typename t_function, typename... t_args>
            auto submit(t_function&& function, t_args&&... args)
            {
                t_lock_guard lg(m_queue_mutex);

                if (thread_belongs_to_pool()) {
                    assert(
                        "worker threads can not submit tasks to the same pool \
                    cause this may lead to dead locks");
                    return std::future<decltype(function(args...))>{};
                }

                auto deferred_call = std::bind(
                    std::forward<t_function>(function), std::forward<t_args>(args)...);

                auto deferred_task =
                    std::make_shared<std::packaged_task<decltype(function(args...))()>>(
                        std::move(deferred_call));

                // Wrap packaged task into void function
                const std::function<void()> task_performer = [deferred_task]() {
                    (*deferred_task)();
                };

                m_task_queue.push(task_performer);

                m_cv.notify_one();
                return deferred_task->get_future();
            }

            bool is_running() const { return m_running; }
            bool has_work() const
            {
                t_lock_guard lg(m_queue_mutex);
                return !m_task_queue.empty() || m_current_workload != 0;
            }

            worker_pool(size_t workers) : m_workers_num(workers) {}

            ~worker_pool() { stop(); }

            worker_pool& operator=(worker_pool&& rhv) noexcept
            {
                stop();
                clear_tasks();

                rhv.stop();

                m_workers_num = rhv.m_workers_num;
                m_task_queue = std::move(rhv.m_task_queue);
                return *this;
            }

            worker_pool(worker_pool&& that) noexcept { *this = std::move(that); }

            void clear_tasks()
            {
                if (m_running) {
                    // this function SHOULD only be called when
                    // worker_pool is not running, the worker_pool will be stopped
                    stop();
                }
                t_lock_guard lg(m_queue_mutex);
                t_task_queue empty;
                std::swap(m_task_queue, empty);
            }
        };
    }  // namespace utils
}  // namespace facade