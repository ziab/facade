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
#include <atomic>
#include <thread>

#include "utils.h"

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>

#include <algorithm/md5.hpp> // digestcpp

#define FACADE_METHOD(_NAME) \
template<typename ...t_args>\
auto _NAME(t_args&& ... args)\
{\
    using t_ret = decltype(m_impl._NAME(std::forward<t_args>(args)...));\
    using t_method = t_ret(t_args...);\
    std::function lambda{ [this](t_args&&... args) -> t_ret {\
        return m_impl._NAME(std::forward<t_args>(args)...);\
    } };\
    return call_method<t_ret>(\
        lambda,\
        #_NAME,\
        std::forward<t_args>(args)...);\
}\

#define FACADE_CONSTRUCTOR(_name) \
_name(t_impl_type& impl, bool record) : facade(#_name, impl, record) {}\
_name(std::unique_ptr<t_impl_type> ptr, bool record) : facade(#_name, std::move(ptr), record) {}\
_name(const std::filesystem::path& file) : facade(#_name, file) {}\

namespace facade
{
    struct method_call;
    struct method_result;
}

namespace cereal
{
    template<class t_archive>
    void serialize(t_archive& archive, facade::method_call& call)
    {
        archive(
            cereal::make_nvp("pre_args", call.pre_args),
            cereal::make_nvp("results", call.results));
    }

    template<class t_archive>
    void serialize(t_archive& archive, facade::method_result& result)
    {
        archive(
            cereal::make_nvp("post_args", result.post_args),
            cereal::make_nvp("ret", result.ret),
            cereal::make_nvp("offest_since_epoch", result.offest_since_epoch),
            cereal::make_nvp("duration", result.duration));
    }
}

namespace facade
{
    using t_duration_resolution = std::chrono::microseconds;
    using t_cereal_output_archive = cereal::JSONOutputArchive;
    using t_cereal_input_archive = cereal::JSONInputArchive;
    using t_hasher = digestpp::md5;

    enum class result_selection
    {
        once = 1,
        cycle
    };

    struct method_result
    {
        std::string post_args;
        std::string ret;
        uint64_t offest_since_epoch;
        uint64_t duration;
    };

    struct method_call
    {
        std::string pre_args;
        std::vector<method_result> results;
        mutable size_t current_result{ 0 };

        const auto& get_next_result(const result_selection selection) const
        {
            if (results.empty()) throw std::logic_error{ "results can't be empty" };
            if (current_result >= results.size()) {
                if (selection == result_selection::once) {
                    throw std::logic_error{ "method results are exceeded for" /*put name here*/ };
                }
                else if (selection == result_selection::cycle) {
                    current_result = 0;
                }
            }
            return results[current_result++];
        }
    };

    template<typename t_archive>
    struct arg_unpacker
    {
        t_archive& archive;
        arg_unpacker(t_archive& _archive) : archive(_archive) {};

        template<typename t_arg>
        void operator()(t_arg& arg)
        {
            if constexpr (std::is_const<t_arg>::value) {
                // If argument has costant qualifier we extract it to a dummy
                // variable to move further through the stream.
                // Can be optimized by skippin the field in the stream
                typename std::decay<t_arg>::type dummy;
                archive(dummy);
            } else {
                archive(arg);
            }
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
                method_call>> m_calls;
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
        result_selection m_selection{ result_selection::cycle };

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

        template <typename t_ret, typename t_method, class ...t_expected_args, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method_play(
            t_method&& method,
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
            const auto& this_method_call_result = this_method_call_it->second.get_next_result(m_selection);
            std::this_thread::sleep_for(t_duration_resolution{ this_method_call_result.duration });
            unpack(this_method_call_result.post_args, std::forward<t_actual_args>(args)...);
            if constexpr (has_return) {
                typename std::decay<t_ret>::type ret{};
                unpack(this_method_call_result.ret, ret);
                return ret;
            }
            if constexpr (has_return) return {};
        }

        void insert_method_call(
            const std::string& method_name, 
            std::string& pre_args, 
            method_result&& result)
        {
            const auto hash = calculate_hash(pre_args);
            t_lock_guard lg(m_mtx);
            auto& method_calls = m_calls[method_name];
            auto method_call_it = method_calls.find(hash);
            if (method_call_it == method_calls.end())
            {
                method_call method_call;
                method_call.pre_args = std::move(pre_args);
                method_call_it = method_calls.insert({ hash, std::move(method_call) }).first;
            }

            method_call_it->second.results.emplace_back(std::move(result));
        }

        template <typename t_ret, typename t_method, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method_and_record(
            t_method&& method,
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            std::string pre_args;
            record_args(pre_args, std::forward<t_actual_args>(args)...);
            method_result this_call_result;
            utils::timer timer;
            std::any ret;
            constexpr const bool has_return = !std::is_same<t_ret, void>::value;
            if constexpr (has_return) {
                ret = method(std::forward<t_actual_args>(args)...);
                record_args(this_call_result.ret, std::any_cast<t_ret>(ret));
            } else {
                method(std::forward<t_actual_args>(args)...);
            }
            this_call_result.duration = timer.get_duration<t_duration_resolution>();
            record_args(this_call_result.post_args, std::forward<t_actual_args>(args)...);
            insert_method_call(method_name, pre_args, std::move(this_call_result));
            if constexpr (has_return) {
                return std::any_cast<t_ret>(ret);
            }
        }

        template <typename t_ret, typename t_method, class ...t_expected_args, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method_pass_through(
            t_method&& method,
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            return method(std::forward<t_actual_args>(args)...);
        }

        template <typename t_ret, typename t_method, typename ...t_actual_args>
        typename std::decay<t_ret>::type call_method(
            t_method&& method,
            const std::string& method_name,
            t_actual_args&& ... args)
        {
            if (m_playing)
            {
                return call_method_play<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
            }
            if (m_recording) {
                return call_method_and_record<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
            }
            else {
                return call_method_pass_through<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
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