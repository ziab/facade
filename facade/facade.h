#pragma once
#include <memory>
#include <any>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <mutex>

#define FACADE_METHOD(_name) \
template<typename ...t_args>\
auto _name(t_args&& ... args)\
{\
    return call_method(\
        m_impl,\
        &t_impl_type::_name,\
        #_name,\
        std::forward<t_args>(args)...);\
}\

#define FACADE_CONSTRUCTOR(_name) \
_name(t_impl_type& impl) : facade(impl) {}\
_name(std::unique_ptr<t_impl_type> ptr) : facade(std::move(ptr)) {}\

namespace facade
{
    struct method_call
    {
        std::vector<std::any> pre_args;
        std::vector<std::any> post_args;
        std::any ret;
        std::chrono::time_point<std::chrono::system_clock> timestamp;
    };

    template<typename t_type>
    class facade
    {
        std::unordered_map<std::string, std::vector<method_call>> m_calls;
        std::mutex m_mtx;
    protected:
        std::unique_ptr<t_type> m_ptr;
        t_type& m_impl;

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;

        inline void visit_args_impl(std::vector<std::any>& arg_storage)
        {
            /* end of variadic recursion */
        }

        template <typename t, typename ...t_args>
        void visit_args_impl(std::vector<std::any>& arg_storage, t&& head, t_args&& ... tail)
        {
            arg_storage.emplace_back(head);
            visit_args_impl(arg_storage, tail...);
        }

        template <typename ...t_args>
        void visit_args(std::vector<std::any>& arg_storage, t_args&& ... args)
        {
            visit_args_impl(arg_storage, std::forward<t_args>(args)...);
        }

        template<typename ...t_args>
        void pre_call(std::vector<std::any>& pre_args, t_args&& ... args)
        {
            visit_args(pre_args, std::forward<t_args>(args)...);
        }

        template<typename ...t_args>
        void post_call(std::vector<std::any>& post_args, t_args&& ... args)
        {
            visit_args(post_args, std::forward<t_args>(args)...);
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        t_ret call_method(
            t_obj& obj, 
            t_ret(t_obj::* method)(t_expected_args...), 
            const std::string& method_name, 
            t_actual_args&& ... args)
        {
            method_call this_call;
            pre_call(this_call.pre_args, std::forward<t_actual_args>(args)...);
            const auto ret = (obj.*method)(std::forward<t_actual_args>(args)...);
            this_call.ret = ret;
            post_call(this_call.post_args, std::forward<t_actual_args>(args)...);
            {
                t_lock_guard lg(m_mtx);
                m_calls[method_name].emplace_back(this_call);
            }
            return ret;
        }

    public:
        using t_impl_type = t_type;

        facade(t_type& impl) : m_impl(impl) {}
        facade(std::unique_ptr<t_type>&& ptr) : m_ptr(std::move(ptr)), m_impl(*m_ptr) {}
    };
}