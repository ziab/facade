#pragma once
#include <memory>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <mutex>
#include <fstream>
#include <string>
#include <filesystem>
#include <type_traits>
#include <any>

#include "utils.h"

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>

#include <algorithm/md5.hpp> // digestcpp

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

#define FACADE_METHOD_CONST(_name) \
template<typename ...t_args>\
auto _name(t_args&& ... args)\
{\
    const auto& const_ref = static_cast<const t_impl_type&>(m_impl);\
    using t_ret = decltype(const_ref._name(std::forward<t_args>(args)...));\
    t_ret(t_impl_type::*func_ptr)(t_args...) const;\
    return call_method(\
        m_impl,\
        func_ptr,\
        #_name,\
        std::forward<t_args>(args)...);\
}\

#define FACADE_TEMPLATE_METHOD(_name) \
template<typename ...t_method_params, typename ...t_args>\
auto _name(t_args&& ... args)\
{\
    using t_ret = decltype(m_impl._name(std::forward<t_args>(args)...));\
    t_ret(t_impl_type::* func_ptr)(t_method_params...) = &t_impl_type::_name;\
    return call_method(\
        m_impl,\
        func_ptr,\
        #_name,\
        std::forward<t_args>(args)...);\
}\

#define FACADE_CONSTRUCTOR(_name) \
_name(t_impl_type& impl, bool record) : facade(#_name, impl, record) {}\
_name(std::unique_ptr<t_impl_type> ptr, bool record) : facade(#_name, std::move(ptr), record) {}\
_name(const std::filesystem::path& file) : facade(#_name, file) {}\

namespace facade
{
    struct method_call;
}

namespace cereal
{
    template<class t_archive>
    void save(t_archive& archive, const std::unique_ptr<facade::method_call>& call)
    {
        archive(
            cereal::make_nvp("pre_args", call->pre_args),
            cereal::make_nvp("post_args", call->post_args),
            cereal::make_nvp("ret", call->ret),
            cereal::make_nvp("duration", call->duration));
    }

    template<class t_archive>
    void load(t_archive& archive, std::unique_ptr<facade::method_call>& call)
    {
        call = std::make_unique<facade::method_call>();
        archive(
            cereal::make_nvp("pre_args", call->pre_args),
            cereal::make_nvp("post_args", call->post_args),
            cereal::make_nvp("ret", call->ret),
            cereal::make_nvp("duration", call->duration));
    }
}

namespace facade
{
    using t_duration_resolution = std::chrono::microseconds;
    using t_cereal_output_archive = cereal::JSONOutputArchive;
    using t_cereal_input_archive = cereal::JSONInputArchive;
    using t_hasher = digestpp::md5;

    struct method_call
    {
        std::string pre_args;
        std::string post_args;
        std::string ret;
        uint64_t offest_since_epoch;
        uint64_t duration;
    };

    template<typename t_archive>
    struct arg_unpacker
    {
        t_archive& archive;
        arg_unpacker(t_archive& _archive) : archive(_archive) {};

        template<typename t_arg>
        void operator()(t_arg& arg)
        {
            archive(arg);
        }
    };

    template<typename ...t_args>
    void unpack(const std::string& recorded, t_args&&... args)
    {
        if (recorded.empty()) return;
        std::stringstream ss;
        ss.str(recorded);
        t_cereal_input_archive archive{ ss };
        arg_unpacker unpacker(archive);
        utils::visit_args(unpacker, std::forward<t_args>(args)...);
    }

    std::string calculate_hash(const std::string& data)
    {
        return t_hasher{}.absorb(data).hexdigest();
    }

    class facade_base
    {
    protected:
        std::unordered_map<
            std::string, 
            std::unordered_map<
                std::string,
                std::unique_ptr<method_call>>> m_calls;
        std::mutex m_mtx;
        std::string m_name;

        const bool m_playing{ false };
        const bool m_recording{ false };
    public:

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;

        void load(const std::filesystem::path& file)
        {
            std::ifstream ifs(file);
            if (!ifs.good()) {
                std::runtime_error{ 
                    std::string{ "failed to load a recording: " } + file.string() };
            }
            t_cereal_input_archive archive{ ifs };

            std::string name;
            archive(cereal::make_nvp("name", name));
            if (name != m_name) {
                std::runtime_error{
                    std::string{ "name in the recording is not matching: " } + name + " " + m_name };
            }

            archive(cereal::make_nvp("calls", m_calls));
        }

        facade_base(std::string name, bool recording) : 
            m_name(std::move(name)), 
            m_recording(recording) {}

        facade_base(std::string name, const std::filesystem::path& file) :
            m_name(std::move(name)),
            m_playing(true)
        {
            load(file);
        }

        void write_calls(const std::filesystem::path& path)
        {
            t_lock_guard lg(m_mtx);
            std::ofstream ofs(path);
            t_cereal_output_archive archive{ ofs };
            archive(cereal::make_nvp("name", m_name), cereal::make_nvp("calls", m_calls));
        }
    };

    template<typename t_type>
    class facade : public facade_base
    {

    protected:
        std::unique_ptr<t_type> m_ptr;
        t_type& m_impl;

        template<typename ...t_args>
        void record_args(std::string& recorded, t_args&& ... args)
        {
            std::stringstream ss;
            {
                t_cereal_output_archive archive{ ss };
                utils::visit_args(archive, std::forward<t_args>(args)...);
            }
            recorded = ss.str();
        }

        template<typename ...t_args>
        void restore_args(std::string& recorded, t_args&& ... args)
        {
            std::stringstream ss{ recorded };
            {
                t_cereal_input_archive archive{ ss };
                utils::visit_args(archive, std::forward<t_args>(args)...);
            }
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method_play(
            const t_obj&,
            t_ret(t_obj::*)(t_expected_args...),
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            constexpr const bool has_return = !std::is_same<t_ret, void>::value;
            const auto method_it = m_calls.find(method_name);
            if (method_it == m_calls.end()) {
                if constexpr (has_return) { return {}; }
                else { return; }
            }
            std::string pre_call_args;
            record_args(pre_call_args, std::forward<t_actual_args>(args)...);
            const auto hash = calculate_hash(pre_call_args);
            const auto& this_method_calls = method_it->second;
            const auto this_method_call_it = this_method_calls.find(hash);
            if (this_method_call_it == method_it->second.end()) {
                if constexpr (has_return) { return {}; }
                else { return; }
            }
            const auto& this_method_call = *this_method_call_it->second;
            std::this_thread::sleep_for(t_duration_resolution{ this_method_call.duration });
            unpack(this_method_call.post_args, std::forward<t_actual_args>(args)...);
            if constexpr (has_return) {
                typename std::decay<t_ret>::type ret{};
                unpack(this_method_call.ret, ret);
                return ret;
            }

            if constexpr (has_return) return {};
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        t_ret call_method_and_record(
            const t_obj& obj,
            t_ret(t_obj::*method)(t_expected_args...),
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            auto this_call = std::make_unique<method_call>();
            record_args(this_call->pre_args, std::forward<t_actual_args>(args)...);
            utils::timer timer;
            constexpr const bool has_return = !std::is_same<t_ret, void>::value;
            std::any ret;
            if constexpr (utils::is_pointer_to_const_member_function<decltype(method)>{}) {
                if constexpr (has_return) {
                    ret = (obj.*method)(std::forward<t_actual_args>(args)...);
                    record_args(this_call->ret, std::any_cast<t_ret>(ret));
                } else {
                    (obj.*method)(std::forward<t_actual_args>(args)...);
                }
            }
            else
            {
                auto& non_const_obj = const_cast<t_obj&>(obj);
                if constexpr (has_return) {
                    ret = (non_const_obj.*method)(std::forward<t_actual_args>(args)...);
                    record_args(this_call->ret, std::any_cast<t_ret>(ret));
                } else {
                    (non_const_obj.*method)(std::forward<t_actual_args>(args)...);
                }
            }
            this_call->duration = timer.get_duration();
            record_args(this_call->post_args, std::forward<t_actual_args>(args)...);
            const auto hash = calculate_hash(this_call->pre_args);
            {
                t_lock_guard lg(m_mtx);
                m_calls[method_name][hash] = std::move(this_call);
            }
            if constexpr (has_return) {
                return std::any_cast<t_ret>(ret);
            }
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        t_ret call_method_pass_through(
            const t_obj& obj,
            t_ret(t_obj::* method)(t_expected_args...),
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            if constexpr (utils::is_pointer_to_const_member_function<decltype(method)>{}) {
                return (obj.*method)(std::forward<t_actual_args>(args)...);
            } else {
                auto& non_const_obj = const_cast<t_obj&>(obj);
                return (non_const_obj.*method)(std::forward<t_actual_args>(args)...);
            }
        }

        template <typename t_obj, typename t_ret, class ...t_expected_args, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method(
            const t_obj& obj,
            t_ret(t_obj::* method)(t_expected_args...),
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            if (m_playing)
            {
                return call_method_play(
                    obj, method, method_name, std::forward<t_actual_args>(args)...);
            }
            if (m_recording) {
                return call_method_and_record(
                    obj, method, method_name, std::forward<t_actual_args>(args)...);
            }
            else {
                return call_method_pass_through(
                    obj, method, method_name, std::forward<t_actual_args>(args)...);
            }
        }

    public:
        using t_impl_type = t_type;
        using t_const_impl_type = typename std::add_const<t_type>::type;

        facade(std::string name, t_type& impl, bool record) : 
            facade_base(std::move(name), record),
            m_impl(impl) {}

        facade(std::string name, std::unique_ptr<t_type>&& ptr, bool record) :
            facade_base(std::move(name), record),
            m_ptr(std::move(ptr)), 
            m_impl(*m_ptr) {}

        facade(std::string name, const std::filesystem::path& file) :
            facade_base(std::move(name), file),
            m_impl(*m_ptr) {}
    };
}