#ifndef FACADE_H
#define FACADE_H
#pragma once
#include <any>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>

#include "master.h"
#include "utils.h"

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>

#include <algorithm/md5.hpp>  // digestcpp

#define FACADE_METHOD(_NAME)                                                       \
    template <typename... t_args>                                                  \
    auto _NAME(t_args&&... args)                                                   \
    {                                                                              \
        using t_ret = decltype(m_impl->_NAME(args...));                            \
        using t_method = t_ret(t_args...);                                         \
        std::function lambda{                                                      \
            [this](t_args&&... args) -> t_ret { return m_impl->_NAME(args...); }}; \
        return call_method<t_ret>(lambda, #_NAME, std::forward<t_args>(args)...);  \
    }

#define FACADE_CALLBACK(_NAME, _RET, ...)                                           \
public:                                                                             \
    using t_cbk_func_##_NAME = std::function<_RET(__VA_ARGS__)>;                    \
                                                                                    \
private:                                                                            \
    t_cbk_func_##_NAME m_cbk_func_##_NAME;                                          \
                                                                                    \
public:                                                                             \
    void register_callback_##_NAME(const t_cbk_func_##_NAME& cbk)                   \
    {                                                                               \
        m_callback_invokers[#_NAME] = [this](const ::facade::function_call& call) { \
            invoke_##_NAME(call);                                                   \
        };                                                                          \
        m_cbk_func_##_NAME = cbk;                                                   \
    }                                                                               \
                                                                                    \
    std::function<_RET(__VA_ARGS__)> get_callback_##_NAME()                         \
    {                                                                               \
        return create_callback_wrapper<_RET, t_cbk_func_##_NAME, __VA_ARGS__>(      \
            m_cbk_func_##_NAME, #_NAME);                                            \
    }                                                                               \
    void invoke_##_NAME(const ::facade::function_call& call)                        \
    {                                                                               \
        std::any ret;                                                               \
        std::tuple<__VA_ARGS__> args;                                               \
        ::facade::unpack_callback<_RET>(call, ret, args);                           \
        std::apply(m_cbk_func_##_NAME, args);                                       \
    }

#define FACADE_CONSTRUCTOR(_NAME)                                             \
    using t_callback_initializer = std::function<void(t_impl_type&, _NAME&)>; \
    _NAME(std::unique_ptr<t_impl_type> ptr, bool record)                      \
        : facade(#_NAME, std::move(ptr), record)                              \
    {                                                                         \
    }                                                                         \
    _NAME() : facade(#_NAME) {}                                               \
    void rewire_callbacks(const t_callback_initializer& rewire)               \
    {                                                                         \
        rewire(*m_impl, *this);                                               \
    }

namespace facade
{
    struct function_call;
    struct function_result;
}  // namespace facade

namespace cereal
{
    template <class t_archive>
    void serialize(t_archive& archive, facade::function_call& call)
    {
        archive(cereal::make_nvp("name", call.name),
            cereal::make_nvp("pre_args", call.pre_args),
            cereal::make_nvp("results", call.results));
    }

    template <class t_archive>
    void serialize(t_archive& archive, facade::function_result& result)
    {
        archive(cereal::make_nvp("post_args", result.post_args),
            cereal::make_nvp("ret", result.ret),
            cereal::make_nvp("offest_since_epoch", result.offest_since_epoch),
            cereal::make_nvp("duration", result.duration));
    }
}  // namespace cereal

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

    struct function_result
    {
        std::string post_args;
        std::string ret;
        uint64_t offest_since_epoch;
        uint64_t duration;  // std::chrono::microseconds
    };

    struct function_call
    {
        std::string name;
        std::string pre_args;
        std::vector<function_result> results;
        mutable size_t current_result{0};

        const auto& get_next_result(const result_selection selection) const
        {
            if (results.empty()) throw std::logic_error{"results can't be empty"};
            if (current_result >= results.size()) {
                if (selection == result_selection::once) {
                    throw std::logic_error{
                        "method results are exceeded for" /*put name here*/};
                } else if (selection == result_selection::cycle) {
                    current_result = 0;
                }
            }
            return results[current_result++];
        }

        auto get_first_offset() const { return results.at(0).offest_since_epoch; }
    };

    template <typename t_archive>
    struct arg_unpacker
    {
        t_archive& archive;
        arg_unpacker(t_archive& _archive) : archive(_archive){};

        template <typename t_arg>
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

    template <typename... t_args>
    void unpack(const std::string& recorded, t_args&&... args)
    {
        if (recorded.empty()) return;
        std::stringstream ss;
        ss.str(recorded);
        t_cereal_input_archive archive{ss};
        arg_unpacker unpacker(archive);
        utils::visit_args(unpacker, std::forward<t_args>(args)...);
    }

    template <typename t_ret, typename... t_actual_args>
    void unpack_callback(
        const function_call& this_call, std::any& ret, std::tuple<t_actual_args...>& args)
    {
    }

    std::string calculate_hash(const std::string& data)
    {
        return t_hasher{}.absorb(data).hexdigest();
    }

    class facade_base : public facade_interface
    {
    protected:
        // clang-format off
        std::unordered_map<
            std::string, 
            std::unordered_map<
                std::string,
                function_call>> m_calls;

        std::list<function_call> m_callbacks;

        std::unordered_map<
            std::string,
            std::function<void(const function_call&)>> m_callback_invokers;

        std::mutex m_mtx;
        const std::string m_name;

        const bool m_playing{ false };
        const bool m_recording{ false };
        // clang-format on

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;
        using t_method_record_inserter = void(const std::string& method_name,
            std::string& pre_args, function_result&& result);

        void facade_load(const std::filesystem::path& file) override
        {
            t_lock_guard lg(m_mtx);
            std::ifstream ifs(file);
            if (!ifs.good()) {
                throw std::runtime_error{
                    std::string{"failed to load a recording: "} + file.string()};
            }
            t_cereal_input_archive archive{ifs};

            std::string name;
            archive(cereal::make_nvp("name", name));
            if (name != m_name) {
                throw std::runtime_error{
                    std::string{"name in the recording is not matching: "} + name + " " +
                    m_name};
            }

            archive(cereal::make_nvp("calls", m_calls),
                cereal::make_nvp("callbacks", m_callbacks));
        }

        bool is_passing_through() const { return !m_playing && !m_recording; }

        void initialize()
        {
            if (!is_passing_through()) { master::get_instance().register_facade(this); }
        }

        facade_base(std::string name, bool recording)
            : m_name(std::move(name)), m_recording(recording)
        {
            initialize();
        }

        facade_base(std::string name)
            : m_name(std::move(name)), m_playing(true)
        {
            initialize();
        }

    public:
        const std::string& facade_name() const override { return m_name; }

        void facade_save(const std::filesystem::path& path) override
        {
            t_lock_guard lg(m_mtx);
            std::ofstream ofs(path);
            t_cereal_output_archive archive{ofs};
            archive(cereal::make_nvp("name", m_name), cereal::make_nvp("calls", m_calls),
                cereal::make_nvp("callbacks", m_callbacks));
        }

        ~facade_base()
        {
            if (!is_passing_through()) { master::get_instance().unregister_facade(this); }
        }
    };

    template <typename t_type>
    class facade : public facade_base
    {
    protected:
        std::unique_ptr<t_type> m_impl;
        result_selection m_selection{result_selection::cycle};

        template <typename... t_args>
        void record_args(std::string& recorded, t_args&&... args)
        {
            std::stringstream ss;
            {
                t_cereal_output_archive archive{ss};
                utils::visit_args(archive, std::forward<t_args>(args)...);
            }
            recorded = ss.str();
        }

        template <typename... t_args>
        void restore_args(std::string& recorded, t_args&&... args)
        {
            std::stringstream ss{recorded};
            {
                t_cereal_input_archive archive{ss};
                utils::visit_args(archive, std::forward<t_args>(args)...);
            }
        }

        template <typename t_ret, typename t_method, class... t_expected_args,
            typename... t_actual_args>
        typename std::decay<t_ret>::type replay_function_call(
            t_method&& method, const std::string& method_name, t_actual_args&&... args)
        {
            constexpr const bool has_return = !std::is_same<t_ret, void>::value;
            const auto method_it = m_calls.find(method_name);
            if (method_it == m_calls.end()) {
                if constexpr (has_return) {
                    return {};
                } else {
                    return;
                }
            }
            std::string pre_call_args;
            record_args(pre_call_args, std::forward<t_actual_args>(args)...);
            const auto hash = calculate_hash(pre_call_args);
            const auto& this_method_calls = method_it->second;
            const auto this_method_call_it = this_method_calls.find(hash);
            if (this_method_call_it == method_it->second.end()) {
                if constexpr (has_return) {
                    return {};
                } else {
                    return;
                }
            }
            const auto& this_method_call_result =
                this_method_call_it->second.get_next_result(m_selection);
            std::this_thread::sleep_for(
                t_duration_resolution{this_method_call_result.duration});
            unpack(
                this_method_call_result.post_args, std::forward<t_actual_args>(args)...);
            if constexpr (has_return) {
                typename std::decay<t_ret>::type ret{};
                unpack(this_method_call_result.ret, ret);
                return ret;
            }
            if constexpr (has_return) return {};
        }

        void insert_method_call(const std::string& method_name, std::string& pre_args,
            function_result&& result)
        {
            const auto hash = calculate_hash(pre_args);
            t_lock_guard lg(m_mtx);
            auto& method_calls = m_calls[method_name];
            auto method_call_it = method_calls.find(hash);
            if (method_call_it == method_calls.end()) {
                function_call function_call;
                function_call.name = method_name;
                function_call.pre_args = std::move(pre_args);
                method_call_it =
                    method_calls.insert({hash, std::move(function_call)}).first;
            }

            method_call_it->second.results.emplace_back(std::move(result));
        }

        void insert_callback_call(const std::string& method_name, std::string& pre_args,
            function_result&& result)
        {
            t_lock_guard lg(m_mtx);
            function_call callback_call;
            callback_call.name = method_name;
            callback_call.pre_args = std::move(pre_args);
            callback_call.results.emplace_back(std::move(result));
            m_callbacks.emplace_back(std::move(callback_call));
        }

        template <typename t_ret, typename t_method, typename... t_actual_args>
        typename std::decay<t_ret>::type call_function_and_record(t_method&& method,
            const std::string& method_name,
            const std::function<t_method_record_inserter>& inserter,
            t_actual_args&&... args)
        {
            std::string pre_args;
            record_args(pre_args, std::forward<t_actual_args>(args)...);
            function_result this_call_result;
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
            inserter(method_name, pre_args, std::move(this_call_result));
            if constexpr (has_return) { return std::any_cast<t_ret>(ret); }
        }

        template <typename t_ret, typename t_method, class... t_expected_args,
            typename... t_actual_args>
        typename std::decay<t_ret>::type pass_through(
            t_method&& method, const std::string& method_name, t_actual_args&&... args)
        {
            return method(std::forward<t_actual_args>(args)...);
        }

        template <typename t_ret, typename t_method, typename... t_actual_args>
        typename std::decay<t_ret>::type call_method(
            t_method&& method, const std::string& method_name, t_actual_args&&... args)
        {
            if (m_playing) {
                return replay_function_call<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
            }
            if (m_recording) {
                auto inserter = [this](const std::string& method_name,
                                    std::string& pre_args,
                                    function_result&& result) -> void {
                    insert_method_call(method_name, pre_args, std::move(result));
                };
                return call_function_and_record<t_ret>(
                    method, method_name, inserter, std::forward<t_actual_args>(args)...);
            } else {
                return pass_through<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
            }
        }

        template <typename t_ret, typename t_method, typename... t_actual_args>
        typename std::decay<t_ret>::type call_callback(
            t_method&& method, const std::string& method_name, t_actual_args&&... args)
        {
            if (m_playing) {
                throw std::runtime_error(
                    "call_callback is not expected to be called during m_playing == "
                    "true");
            }
            if (m_recording) {
                auto inserter = [this](const std::string& method_name,
                                    std::string& pre_args,
                                    function_result&& result) -> void {
                    insert_callback_call(method_name, pre_args, std::move(result));
                };
                return call_function_and_record<t_ret>(
                    method, method_name, inserter, std::forward<t_actual_args>(args)...);
            } else {
                return pass_through<t_ret>(
                    method, method_name, std::forward<t_actual_args>(args)...);
            }
        }

        template <typename t_ret, typename t_method, typename... t_actual_args>
        auto create_callback_wrapper(t_method method, const std::string& method_name)
        {
            return [this, method, method_name](t_actual_args... args) -> t_ret {
                return call_callback<t_ret>(method, method_name, args...);
            };
        }

    public:
        using t_impl_type = t_type;
        using t_const_impl_type = typename std::add_const<t_type>::type;

        facade(std::string name, std::unique_ptr<t_type>&& ptr, bool record)
            : facade_base(std::move(name), record), m_impl(std::move(ptr))
        {
        }

        facade(std::string name) : facade_base(std::move(name)) {}
    };
}  // namespace facade

#endif