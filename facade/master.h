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
        virtual const std::string& facade_name() const = 0;
    };

    class master
    {
        std::set<facade_interface*> m_facades;
        utils::worker_pool m_pool{1};
        std::mutex m_mtx;

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;

    protected:
        void register_facade(facade_interface* facade)
        {
            t_lock_guard lg{m_mtx};
            m_facades.insert(facade);
        }
        void unregister_facade(facade_interface* facade)
        {
            t_lock_guard lg{m_mtx};
            m_facades.erase(facade);
        }

        master() {}

    public:
        friend class facade_base;

        static master& get_instance()
        {
            static std::unique_ptr<master> m_instance;
            if (!m_instance) { m_instance = std::unique_ptr<master>(new master); }
            return *m_instance;
        }

        void save_recordings(
            const std::string& directory = ".", const std::string& extention = ".json")
        {
            t_lock_guard lg{m_mtx};
            for (auto* facade : m_facades) {
                std::filesystem::path path = std::filesystem::path{directory} /
                    (facade->facade_name() + extention);
                facade->facade_save(path);
            }
        }

        void load_recordings(
            const std::string& directory = ".", const std::string& extention = ".json")
        {
            t_lock_guard lg{m_mtx};
            for (auto* facade : m_facades) {
                std::filesystem::path path = std::filesystem::path{directory} /
                    (facade->facade_name() + extention);
                facade->facade_load(path);
            }
        }
    };

    inline ::facade::master& master() { return ::facade::master::get_instance(); }
}  // namespace facade
#endif