#ifndef MASTER_H
#define MASTER_H
#pragma once
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef __cpp_lib_filesystem
#include <filesystem>
#else
#include <experimental/filesystem>
namespace std
{
    // Map std::filesystem to std::experimental::filesystem for those who have older compilers
    namespace filesystem = std::experimental::filesystem;
}  // namespace std
#endif

#include "utils.h"
#include "worker_pool.h"

namespace facade
{
    using namespace std::chrono_literals;

    enum class result_selection
    {
        once = 1,
        cycle
    };

    struct function_result
    {
        std::string post_call_args;
        std::string return_value;
        t_duration offest_from_origin;
        t_duration duration;
    };

    struct function_call
    {
        std::string function_name;
        std::string pre_call_args;
        std::vector<function_result> results;
        mutable size_t current_result{0};

        const auto& get_next_result(const result_selection selection) const
        {
            if (results.empty()) throw std::logic_error{"results can't be empty"};
            if (current_result >= results.size()) {
                if (selection == result_selection::once) {
                    throw std::logic_error{
                        "method results are exceeded for " + function_name};
                } else if (selection == result_selection::cycle) {
                    current_result = 0;
                }
            }
            return results[current_result++];
        }

        auto get_first_offset() const { return results.at(0).offest_from_origin; }
    };

    enum class facade_mode
    {
        passthrough = 0,
        recording,
        playing,
    };

    // The reason this interface is needed is to break circular dependency between the
    // master and facade(facade_base)
    class facade_interface
    {
    protected:
        // These prefixes are added mainly to avoid method name clashing with
        // methods in the original class implementation
        virtual void facade_save(const std::filesystem::path& path) = 0;
        virtual void facade_load(const std::filesystem::path& path) = 0;
        virtual void facade_clear() = 0;
        virtual const std::string& facade_name() const = 0;
        virtual const std::list<function_call>& get_callbacks() const = 0;
        virtual void invoke_callback(const function_call& callback) = 0;

    public:
        friend class master;
        friend class scheduled_callback_entry;
    };

    class facade_proxy
    {
        std::atomic_int64_t m_active_users{0};
        std::mutex m_mtx;
        std::condition_variable m_cv;
        facade_interface* m_facade;

    public:
        facade_proxy(facade_interface* facade_ptr) : m_facade(facade_ptr) {}
        facade_proxy(const facade_proxy&) = delete;
        facade_proxy& operator=(const facade_proxy&) = delete;

        facade_interface* ref()
        {
            std::lock_guard<std::mutex> lg{m_mtx};
            if (!m_facade) return nullptr;
            m_active_users.fetch_add(1);
            return m_facade;
        }

        void unref()
        {
            std::lock_guard<std::mutex> lg{m_mtx};
            if (!m_facade) return;
            m_active_users.fetch_sub(1);
            m_cv.notify_all();
        }

        void teardown()
        {
            std::unique_lock<std::mutex> ul{m_mtx};
            while (m_active_users.load() != 0) { m_cv.wait(ul); }
            m_facade = nullptr;
            m_cv.notify_all();
        }

        operator bool() const { return m_facade != nullptr; }
        facade_interface* operator->() { return m_facade; }
        facade_interface& operator*() { return *m_facade; }
    };

    class scheduled_callback_entry
    {
        const t_duration m_offset;
        const function_call& m_call;
        std::shared_ptr<facade_proxy> m_facade_proxy;

    public:
        bool operator<(const scheduled_callback_entry& rhv) const
        {
            return m_offset < rhv.m_offset;
        }

        scheduled_callback_entry(
            const function_call& cbk, std::shared_ptr<facade_proxy> facade)
            : m_offset(cbk.get_first_offset()),
              m_call(cbk),
              m_facade_proxy(std::move(facade))
        {
        }

        scheduled_callback_entry(const scheduled_callback_entry& that)
            : m_offset(that.m_offset),
              m_call(that.m_call),
              m_facade_proxy(that.m_facade_proxy)
        {
        }

        auto offset() const { return m_offset; }

        void invoke(const t_highres_timepoint& origin) const
        {
            auto* facade = m_facade_proxy->ref();
            // if nullptr is returned then the facade has been deleted
            if (!facade) return;
            utils::sleep_until(origin, m_offset);
            try {
                facade->invoke_callback(m_call);
            } catch (...) {
                // TODO: report exception
            }
            m_facade_proxy->unref();
        }
    };

    class master
    {
        mutable std::mutex m_mtx;
        mutable std::condition_variable m_cv;
        std::map<facade_interface*, std::shared_ptr<facade_proxy>> m_facades;
        utils::worker_pool m_pool{1};
        std::filesystem::path m_recording_dir;
        std::string m_recording_file_extention;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_origin;
        std::multiset<scheduled_callback_entry> m_callbacks;
        std::thread m_player_thread;

        facade_mode m_mode{facade_mode::passthrough};
        bool m_override_arguments{true};

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;
        using t_unique_lock = std::unique_lock<decltype(m_mtx)>;

        void initialize(facade_interface& facade)
        {
            if (is_playing()) { facade.facade_load(make_recording_path(facade)); }
        }

        void finalize(facade_interface& facade)
        {
            if (is_recording()) { facade.facade_save(make_recording_path(facade)); }
            facade.facade_clear();
        }

        void unprotected_register_callbacks(const std::shared_ptr<facade_proxy>& facade)
        {
            const auto& callbacks = (*facade)->get_callbacks();
            for (const auto& cbk : callbacks) {
                scheduled_callback_entry entry{cbk, facade};
                m_callbacks.insert(entry);
            }
        }

        void player_thread_main()
        {
            while (true) {
                t_unique_lock ulck(m_mtx);
                while (m_callbacks.empty() && m_mode == facade_mode::playing) {
                    m_cv.wait(ulck);
                }

                if (m_mode != facade_mode::playing) return;

                const auto it = m_callbacks.begin();
                auto callback_entry{std::move(*it)};
                m_callbacks.erase(it);
                auto origin = m_origin;
                m_pool.submit([callback_entry, origin = std::move(origin)]() {
                    callback_entry.invoke(origin);
                });

                m_cv.notify_all();
            }
        }

    protected:
        void register_facade(facade_interface* facade)
        {
            initialize(*facade);
            {
                t_lock_guard lg{m_mtx};
                auto&& [it, inserted] =
                    m_facades.insert({facade, std::make_shared<facade_proxy>(facade)});
                unprotected_register_callbacks(it->second);
                m_cv.notify_all();
            }
        }
        void unregister_facade(facade_interface* facade)
        {
            std::shared_ptr<facade_proxy> proxy_shptr;
            {
                t_lock_guard lg{m_mtx};
                const auto found = m_facades.find(facade);
                if (found == m_facades.end()) return;
                proxy_shptr = std::move(found->second);
                m_facades.erase(found);
                m_cv.notify_all();
            }
            // this will ensure that facade is not replaying any recoded callbacks
            // and it's safe to delete it now
            proxy_shptr->teardown();
            finalize(*facade);
        }

        master() {}

        void unprotected_save_recordings()
        {
            for (const auto& [_unused, facade_proxy_shptr] : m_facades) {
                if (!facade_proxy_shptr) continue;
                auto& facade = **facade_proxy_shptr;
                const auto path = make_recording_path(facade);
                facade.facade_save(path);
                facade.facade_clear();
            }
        }

        void unprotected_load_recordings()
        {
            m_callbacks.clear();
            for (const auto& [_unused, facade_proxy_shptr] : m_facades) {
                if (!facade_proxy_shptr) continue;
                auto& facade = **facade_proxy_shptr;
                const auto path = make_recording_path(facade);
                facade.facade_clear();
                facade.facade_load(path);
                unprotected_register_callbacks(facade_proxy_shptr);
            }
        }

    public:
        friend class facade_base;

        bool is_passing_through() const { return m_mode == facade_mode::passthrough; }
        bool is_playing() const { return m_mode == facade_mode::playing; }
        bool is_recording() const { return m_mode == facade_mode::recording; }

        bool is_overriding_arguments() const { return m_override_arguments; }
        void override_arguments(const bool enabled) { m_override_arguments = enabled; }

        std::filesystem::path make_recording_path(const facade_interface& facade) const 
        {
            return std::filesystem::path{m_recording_dir} /
                (facade.facade_name() + m_recording_file_extention);
        }

        static master& get_instance()
        {
            static std::unique_ptr<master> m_instance;
            if (!m_instance) { m_instance = std::unique_ptr<master>(new master); }
            return *m_instance;
        }

        master& set_recording_directory(
            const std::string& directory = ".", const std::string& extention = ".json")
        {
            t_lock_guard lg{m_mtx};
            m_recording_dir = directory;
            m_recording_file_extention = extention;
            return *this;
        }

        auto get_offset_from_origin() const
        {
            return std::chrono::duration_cast<t_duration>(
                std::chrono::high_resolution_clock::now() - m_origin);
        }

        void set_number_of_workers(size_t workers)
        {
            t_lock_guard lg{m_mtx};
            if (!is_passing_through()) return;
            m_pool = utils::worker_pool{workers};
        }

        void start_recording()
        {
            stop();

            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::recording;
            m_origin = std::chrono::high_resolution_clock::now();
        }

        void start_playing()
        {
            stop();

            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::playing;
            unprotected_load_recordings();
            m_origin = std::chrono::high_resolution_clock::now();
            m_pool.start();
            m_player_thread = std::thread{[this]() { player_thread_main(); }};
        }

        void wait_all_pending_callbacks_replayed() const
        {
            t_unique_lock ulck(m_mtx);
            while (!m_callbacks.empty() && m_mode == facade_mode::playing) {
                m_cv.wait(ulck);
            }
            // when all pending callback entries are removed from the queue or
            // playing has stopped we need to wait until all callback calls
            // that are being processed by the worker pool have completed
            m_pool.wait_completion();
        }

        void stop()
        {
            if (is_passing_through()) return;
            {
                t_lock_guard lg{m_mtx};
                m_pool.stop();
                if (is_recording()) unprotected_save_recordings();
                m_mode = facade_mode::passthrough;
                m_cv.notify_all();  // notfiy that m_player_thread should stop
            }
            if (m_player_thread.joinable()) m_player_thread.join();
            m_cv.notify_all();  // notfiy wait_all_pending_callbacks_replayed
        }

        ~master() { stop(); }
    };

    inline ::facade::master& master() { return ::facade::master::get_instance(); }
}  // namespace facade
#endif