#pragma once
#include <memory>
#include <any>
#include <unordered_map>
#include <chrono>

#define FACADE_METHOD(_name) \
template<typename ...t_args>\
auto _name(t_args&& ... args)\
{\
    return call_method(\
        m_impl,\
        &t_impl_type::_name,\
        std::forward<t_args>(args)...);\
}\

#define FACADE_CONSTRUCTOR(_name) \
_name(t_impl_type& impl) : facade(impl) {}\
_name(std::unique_ptr<t_impl_type> ptr) : facade(std::move(ptr)) {}\

namespace facade
{
    struct method_call
    {
        std::vector<std::any> args;
        std::any ret;
        std::chrono::time_point<std::chrono::system_clock> timestamp;
    };

    template<typename t_type>
    class facade
    {
    protected:
        std::unique_ptr<t_type> m_ptr;
        t_type& m_impl;
        std::unordered_map<std::string, std::vector<method_call>> m_calls;

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        t_ret call_method(t_obj& obj, t_ret(t_obj::* method)(t_expected_args...), t_actual_args&& ... args)
        {
            return (obj.*method)(std::forward<t_actual_args>(args)...);
        }

    public:
        using t_impl_type = t_type;

        facade(t_type& impl) : m_impl(impl) {}
        facade(std::unique_ptr<t_type>&& ptr) : m_ptr(std::move(ptr)), m_impl(*m_ptr) {}
    };
}