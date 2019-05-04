﻿#pragma once
#include <memory>
#include <any>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <mutex>
#include <fstream>
#include <string>
#include <filesystem>

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>

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
_name(t_impl_type& impl) : facade(#_name, impl) {}\
_name(std::unique_ptr<t_impl_type> ptr) : facade(#_name, std::move(ptr)) {}\

namespace facade
{
    struct method_call;
}

namespace cereal
{
    template<class t_archive>
    void serialize(t_archive& archive, std::unique_ptr<facade::method_call>& call)
    {
        archive(
            cereal::make_nvp("pre_args", call->pre_args.str()),
            cereal::make_nvp("post_args", call->post_args.str()),
            cereal::make_nvp("ret", call->ret.str()));
    }
}

namespace facade
{
    using t_cereal_archive = cereal::JSONOutputArchive;
    struct method_call
    {
        std::stringstream pre_args;
        std::stringstream post_args;
        std::stringstream ret;
    };

    template <typename t_visitor>
    inline void visit_args_impl(t_visitor&)
    {
        /* end of variadic recursion */
    }

    template <typename t_visitor, typename t, typename ...t_args>
    void visit_args_impl(t_visitor& visitor, t&& head, t_args&& ... tail)
    {
        visitor(head);
        visit_args_impl(visitor, tail...);
    }

    template <typename t_visitor, typename ...t_args>
    void visit_args(t_visitor& visitor, t_args&& ... args)
    {
        visit_args_impl(visitor, std::forward<t_args>(args)...);
    }

    template<typename t_type>
    class facade
    {
        std::unordered_map<std::string, std::vector<std::unique_ptr<method_call>>> m_calls;
        std::mutex m_mtx;
        std::string m_name;
    protected:
        std::unique_ptr<t_type> m_ptr;
        t_type& m_impl;

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;

        template<typename ...t_args>
        void record_pre_call(method_call& mc, t_args&& ... args)
        {
            t_cereal_archive archive{ mc.pre_args };
            visit_args(archive, std::forward<t_args>(args)...);
        }

        template<typename ...t_args>
        void record_post_call(method_call& mc, t_args&& ... args)
        {
            t_cereal_archive archive{ mc.post_args };
            visit_args(archive, std::forward<t_args>(args)...);
        }

        template<typename t_ret>
        void record_return(method_call& mc, t_ret&& ret)
        {
            t_cereal_archive archive{ mc.ret };
            visit_args(archive, ret);
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        t_ret call_method(
            t_obj& obj,
            t_ret(t_obj::* method)(t_expected_args...),
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            const auto this_call = new method_call;
            record_pre_call(*this_call, std::forward<t_actual_args>(args)...);

            const auto ret = (obj.*method)(std::forward<t_actual_args>(args)...);
            record_return(*this_call, ret);
            record_post_call(*this_call, std::forward<t_actual_args>(args)...);

            {
                t_lock_guard lg(m_mtx);
                m_calls[method_name].emplace_back(this_call);
            }
            return ret;
        }

    public:
        using t_impl_type = t_type;

        facade(std::string name, t_type& impl) : 
            m_name(std::move(name)), 
            m_impl(impl) {}
        facade(std::string name, std::unique_ptr<t_type>&& ptr) : 
            m_name(std::move(name)), 
            m_ptr(std::move(ptr)), 
            m_impl(*m_ptr) {}

        void write_calls(const std::filesystem::path& path)
        {
            std::ofstream ofs(path);
            cereal::JSONOutputArchive archive{ ofs };
            archive(cereal::make_nvp("name", m_name), cereal::make_nvp("calls", m_calls));
        }
    };
}