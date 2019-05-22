#ifndef MASTER_H
#define MASTER_H
#pragma once
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "worker_pool.h"

namespace facade
{
    using namespace std::chrono_literals;
    using t_duration = std::chrono::microseconds;

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

    // The reason this interface is needed is to break circular dependency between the
    // master and facade(facade_base)
    class facade_interface
    {
    public:
        // These prefixes are added mainly to avoid method name clashing with
        // methods in the original class implementation
        virtual void facade_save(const std::filesystem::path& path) = 0;
        virtual void facade_load(const std::filesystem::path& path) = 0;
        virtual void facade_clear() = 0;
        virtual const std::string& facade_name() const = 0;
        virtual const std::list<function_call>& get_callbacks() const = 0;
        virtual void invoke_callback(const function_call& callback) = 0;
    };

    enum class facade_mode
    {
        passthrough = 0,
        recording,
        playing,
    };

    struct scheduled_callback_entry
    {
        const t_duration offset;
        const function_call& call;
        facade_interface& facade;

        bool operator<(const scheduled_callback_entry& rhv) const
        {
            return offset < rhv.offset;
        }

        scheduled_callback_entry(const function_call& _cbk, facade_interface& _facade)
            : offset(_cbk.get_first_offset()), call(_cbk), facade(_facade)
        {
        }

        scheduled_callback_entry(const scheduled_callback_entry& that)
            : offset(that.offset), call(that.call), facade(that.facade)
        {
        }

        void invoke() const { facade.invoke_callback(call); }
    };

    class master
    {
        mutable std::mutex m_mtx;
        mutable std::condition_variable m_cv;
        std::set<facade_interface*> m_facades;
        utils::worker_pool m_pool{1};
        std::filesystem::path m_recording_dir;
        std::string m_recording_file_extention;
        std::chrono::time_point<std::chrono::high_resolution_clock> m_origin;
        std::multiset<scheduled_callback_entry> m_callbacks;
        std::thread m_player_thread;

        facade_mode m_mode{facade_mode::passthrough};

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;
        using t_unique_lock = std::unique_lock<decltype(m_mtx)>;

        void initialize(facade_interface& facade)
        {
            if (is_playing()) { facade.facade_load(make_recording_path(facade)); }
        }

        void finalize(facade_interface& facade)
        {
            if (is_recording()) { facade.facade_save(make_recording_path(facade)); }
        }

        void unprotected_register_callbacks(facade_interface& facade)
        {
            const auto& callbacks = facade.get_callbacks();
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
                sleep_until(callback_entry.offset);
                m_pool.submit([callback_entry]() { callback_entry.invoke(); });

                m_cv.notify_all();
            }
        }

    protected:
        void register_facade(facade_interface* facade)
        {
            initialize(*facade);
            {
                t_lock_guard lg{m_mtx};
                m_facades.insert(facade);
                unprotected_register_callbacks(*facade);
                m_cv.notify_all();
            }
        }
        void unregister_facade(facade_interface* facade)
        {
            {
                t_lock_guard lg{m_mtx};
                m_facades.erase(facade);
                m_cv.notify_all();
            }
            finalize(*facade);
        }

        master() {}

        void unprotected_save_recordings()
        {
            for (auto* facade : m_facades) {
                facade->facade_save(make_recording_path(*facade));
                facade->facade_clear();
            }
        }

        void unprotected_load_recordings()
        {
            m_callbacks.clear();
            for (auto* facade : m_facades) {
                facade->facade_clear();
                facade->facade_load(make_recording_path(*facade));
                unprotected_register_callbacks(*facade);
            }
        }

    public:
        friend class facade_base;

        bool is_passing_through() const { return m_mode == facade_mode::passthrough; }
        bool is_playing() const { return m_mode == facade_mode::playing; }
        bool is_recording() const { return m_mode == facade_mode::recording; }

        std::filesystem::path make_recording_path(const facade_interface& facade)
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

        void sleep_until(const t_duration& that_offset_form_origin) const
        {
            const auto this_offset = get_offset_from_origin();
            const auto diff = that_offset_form_origin - this_offset;
            if (diff < t_duration::zero()) return;
            std::this_thread::sleep_for(diff);
        }

        void start_recording()
        {
            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::recording;
            m_origin = std::chrono::high_resolution_clock::now();
        }

        void start_playing()
        {
            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::playing;
            unprotected_load_recordings();
            m_origin = std::chrono::high_resolution_clock::now();
            m_pool.start();
            m_player_thread = std::thread{[this]() { player_thread_main(); }};
        }

        void wait_all_pending_callbacks_replayed() const {
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
            {
                t_lock_guard lg{m_mtx};
                m_pool.stop();
                if (is_recording()) unprotected_save_recordings();
                m_mode = facade_mode::passthrough;
                m_cv.notify_all(); // notfiy that m_player_thread should stop
            }
            if (m_player_thread.joinable()) m_player_thread.join();
            m_cv.notify_all();  // notfiy wait_all_pending_callbacks_replayed 
        }

        ~master() { stop(); }
    };

    inline ::facade::master& master() { return ::facade::master::get_instance(); }
}  // namespace facade
#endif