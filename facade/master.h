#ifndef MASTER_H
#define MASTER_H
#pragma once
#include <filesystem>
#include <memory>
#include <set>

#include "worker_pool.h"

namespace facade
{
    // The reason this interface is need is to break circular dependency betwean the
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
    };

    enum class facade_mode
    {
        passthrough = 0,
        recording,
        playing,
    };

    class master
    {
        std::set<facade_interface*> m_facades;
        utils::worker_pool m_pool{1};
        std::mutex m_mtx;
        std::filesystem::path m_recording_dir;
        std::string m_recording_file_extention;

        facade_mode m_mode;

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;

        void initialize(facade_interface& facade)
        {
            if (is_playing()) { facade.facade_load(make_recording_path(facade)); }
        }

        void finalize(facade_interface& facade)
        {
            if (is_recording()) { facade.facade_save(make_recording_path(facade)); }
        }

    protected:
        void register_facade(facade_interface* facade)
        {
            initialize(*facade);
            t_lock_guard lg{m_mtx};
            m_facades.insert(facade);
        }
        void unregister_facade(facade_interface* facade)
        {
            {
                t_lock_guard lg{m_mtx};
                m_facades.erase(facade);
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
            for (auto* facade : m_facades) {
                facade->facade_clear();
                facade->facade_load(make_recording_path(*facade));
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

        void start_recording()
        {
            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::recording;
        }

        void start_playing()
        {
            t_lock_guard lg{m_mtx};
            m_mode = facade_mode::playing;
            unprotected_load_recordings();
        }

        void stop()
        {
            t_lock_guard lg{m_mtx};
            if (is_recording()) unprotected_save_recordings();
            m_mode = facade_mode::passthrough;
        }
    };

    inline ::facade::master& master() { return ::facade::master::get_instance(); }
}  // namespace facade
#endif