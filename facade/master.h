#include <set>
#include <memory>

#include "facade.h"
#include "worker_pool.h"

namespace facade
{
    class facade_base;

    class master
    {
        std::set<facade_base*> m_facades;
        utils::worker_pool m_pool{ 1 };
        std::mutex m_mtx;

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;
    protected:
        void register_facade(facade_base* facade)
        {
            t_lock_guard lg{ m_mtx };
            m_facades.insert(facade);
        }
        void unregister_facade(facade_base* facade)
        {
            t_lock_guard lg{ m_mtx };
            m_facades.erase(facade);
        }

        master() {}
    public:
        friend class facade_base;

        static master& get_instance()
        {
            static std::unique_ptr<master> m_instance;
            if (!m_instance){
                m_instance = std::unique_ptr<master>(new master);
            }
            return *m_instance;
        }
    };
}