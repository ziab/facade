#ifndef FACADE_H
#define FACADE_H
#pragma once
#include <any>
#include <chrono>
#include <fstream>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
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

#define FACADE_CHECK_OPTIONAL_METHODS(_NAME)      \
private:                                          \
    FACADE_CREATE_MEMBER_CHECK(filter_##_NAME);   \
    FACADE_CREATE_MEMBER_CHECK(override_##_NAME); \
                                                  \
public:

#define FACADE_METHOD(_NAME)                                                            \
    FACADE_CHECK_OPTIONAL_METHODS(_NAME)                                                \
    template <typename... t_args>                                                       \
    auto _NAME(t_args&&... args)                                                        \
    {                                                                                   \
        using t_ret = decltype(m_impl->_NAME(args...));                                 \
        using t_method = t_ret(t_args...);                                              \
        using t_filter_and_record = void(std::string&, t_args...);                      \
        std::function method{                                                           \
            [this](t_args&&... args) -> t_ret { return m_impl->_NAME(args...); }};      \
        std::function<t_method> overrider;                                              \
        if constexpr (FACADE_HAS_MEMBER(t_this_type, override_##_NAME)) {               \
            if (is_overriding_arguments()) {                                            \
                overrider = [this](t_args&&... args) -> t_ret {                         \
                    return override_##_NAME(args...);                                   \
                };                                                                      \
            }                                                                           \
        }                                                                               \
        std::function<t_filter_and_record> filter;                                      \
        if constexpr (FACADE_HAS_MEMBER(t_this_type, filter_##_NAME)) {                 \
            if (is_overriding_arguments()) {                                            \
                auto filter_trampoline = [this](auto&... args) -> t_ret {               \
                    return filter_##_NAME(args...);                                     \
                };                                                                      \
                filter = [filter_trampoline](std::string& recording, auto&&... args) {  \
                    ::facade::filter_args_and_record(                                   \
                        filter_trampoline, recording, args...);                         \
                };                                                                      \
            }                                                                           \
        }                                                                               \
        ::facade::function_call_context ctx{                                            \
            #_NAME, false, std::move(method), std::move(overrider), std::move(filter)}; \
        return call_method<t_ret>(ctx, std::forward<t_args>(args)...);                  \
    }

#define FACADE_STATIC_METHOD(_NAME)                                                    \
    FACADE_CHECK_OPTIONAL_METHODS(_NAME)                                               \
    template <typename... t_args>                                                      \
    static auto _NAME(t_args&&... args)                                                \
    {                                                                                  \
        using t_ret = decltype(t_impl_type::_NAME(args...));                           \
        using t_method = t_ret(t_args...);                                             \
        using t_filter_and_record = void(std::string&, t_args...);                     \
        std::function method{                                                          \
            [](t_args&&... args) -> t_ret { return t_impl_type::_NAME(args...); }};    \
        std::function<t_method> overrider;                                             \
        if constexpr (FACADE_HAS_STATIC(t_this_type, override_##_NAME)) {              \
            if (is_overriding_arguments()) {                                           \
                overrider = [](t_args&&... args) -> t_ret {                            \
                    return override_##_NAME(args...);                                  \
                };                                                                     \
            }                                                                          \
        }                                                                              \
        std::function<t_filter_and_record> filter;                                     \
        if constexpr (FACADE_HAS_STATIC(t_this_type, filter_##_NAME)) {                \
            if (is_overriding_arguments()) {                                           \
                auto filter_trampoline = [](auto&... args) -> t_ret {                  \
                    return filter_##_NAME(args...);                                    \
                };                                                                     \
                filter = [filter_trampoline](std::string& recording, auto&&... args) { \
                    ::facade::filter_args_and_record(                                  \
                        filter_trampoline, recording, args...);                        \
                };                                                                     \
            }                                                                          \
        }                                                                              \
        ::facade::function_call_context ctx{                                           \
            #_NAME, true, std::move(method), std::move(overrider), std::move(filter)}; \
        return get_facade_instance().call_method<t_ret>(                               \
            ctx, std::forward<t_args>(args)...);                                       \
    }

// TODO : improve this, callback invokers should (probably) be added on construction
#define FACADE_CALLBACK(_NAME, _RET, ...)                                              \
    FACADE_CHECK_OPTIONAL_METHODS(_NAME)                                               \
public:                                                                                \
    using t_cbk_func_##_NAME = std::function<_RET(__VA_ARGS__)>;                       \
                                                                                       \
private:                                                                               \
    t_cbk_func_##_NAME m_cbk_func_##_NAME;                                             \
                                                                                       \
public:                                                                                \
    void register_callback_##_NAME(const t_cbk_func_##_NAME& cbk)                      \
    {                                                                                  \
        t_lock_guard lg(m_mtx);                                                        \
        m_callback_invokers[#_NAME] = [this](const ::facade::function_call& call) {    \
            invoke_##_NAME(call);                                                      \
        };                                                                             \
        m_cbk_func_##_NAME = cbk;                                                      \
    }                                                                                  \
                                                                                       \
    template <typename t_type = void>                                                  \
    std::function<_RET(__VA_ARGS__)> get_callback_##_NAME()                            \
    {                                                                                  \
        using t_filter_and_record = void(std::string&, ##__VA_ARGS__);                 \
        auto method = [this](auto&&... args) -> _RET {                                 \
            return m_cbk_func_##_NAME(args...);                                        \
        };                                                                             \
        std::function<t_filter_and_record> filter;                                     \
        if constexpr (FACADE_HAS_MEMBER(t_this_type, filter_##_NAME)) {                \
            if (is_overriding_arguments()) {                                           \
                auto filter_trampoline = [this](auto&... args) -> _RET {               \
                    return filter_##_NAME(args...);                                    \
                };                                                                     \
                filter = [filter_trampoline](std::string& recording, auto&&... args) { \
                    ::facade::filter_args_and_record(                                  \
                        filter_trampoline, recording, args...);                        \
                };                                                                     \
            }                                                                          \
        }                                                                              \
        std::string name = #_NAME;                                                     \
        ::facade::function_call_context ctx{#_NAME, false, method,                     \
            std::function<t_filter_and_record>{}, std::move(filter)};                  \
        return create_callback_wrapper<_RET, decltype(ctx), ##__VA_ARGS__>(ctx);       \
    }                                                                                  \
                                                                                       \
    void invoke_##_NAME(const ::facade::function_call& call)                           \
    {                                                                                  \
        const auto& cbk = m_cbk_func_##_NAME;                                          \
        if (!cbk) return;                                                              \
        using t_decayed_function =                                                     \
            ::facade::utils::get_non_cv_ref_signature<_RET, ##__VA_ARGS__>::type;      \
        std::function<t_decayed_function> overrider;                                   \
        if constexpr (FACADE_HAS_MEMBER(t_this_type, override_##_NAME)) {              \
            if (is_overriding_arguments()) {                                           \
                overrider = [this](auto&... args) -> _RET {                            \
                    return override_##_NAME(args...);                                  \
                };                                                                     \
            }                                                                          \
        }                                                                              \
        std::string name = #_NAME;                                                     \
        ::facade::function_call_context ctx{std::move(name), false, cbk,               \
            std::move(overrider), std::function<t_decayed_function>{}};                \
        ::facade::invoke_callback<decltype(ctx), _RET, ##__VA_ARGS__>(ctx, call);      \
    }

#define FACADE_CONSTRUCTOR(_NAME)                                             \
    using t_this_type = _NAME;                                                \
    _NAME(std::unique_ptr<t_impl_type> ptr) : facade(#_NAME, true)            \
    {                                                                         \
        m_impl = ptr.release();                                               \
    }                                                                         \
    _NAME() : facade(#_NAME, true) {}                                         \
    ~_NAME() { delete m_impl; }                                               \
    void set_impl(std::unique_ptr<t_impl_type>&& impl_ptr)                    \
    {                                                                         \
        m_impl = impl_ptr.release();                                          \
    }                                                                         \
    using t_callback_initializer = std::function<void(t_impl_type&, _NAME&)>; \
    void rewire_callbacks(const t_callback_initializer& rewire)               \
    {                                                                         \
        rewire(*m_impl, *this);                                               \
    }

#define FACADE_SINGLETON_CONSTRUCTOR(_NAME)                                   \
    using t_this_type = _NAME;                                                \
                                                                              \
private:                                                                      \
    _NAME() : facade(#_NAME, false) {}                                        \
                                                                              \
public:                                                                       \
    ~_NAME() { m_impl = nullptr; }                                            \
    void set_impl(t_impl_type* impl_ptr) { m_impl = impl_ptr; }               \
    using t_callback_initializer = std::function<void(t_impl_type&, _NAME&)>; \
    void rewire_callbacks(const t_callback_initializer& rewire)               \
    {                                                                         \
        rewire(*m_impl, *this);                                               \
    }                                                                         \
    static auto& get_facade_instance()                                        \
    {                                                                         \
        static std::unique_ptr<_NAME> m_instance;                             \
        if (!m_instance) m_instance = std::unique_ptr<_NAME>(new _NAME);      \
        return *m_instance;                                                   \
    }                                                                         \
    void register_facade() { internal_register(); }                           \
    void unregister_facade() { internal_unregister(); }

namespace facade
{
    struct function_call;
    struct function_result;
}  // namespace facade

namespace cereal
{
    template <class t_archive>
    uint64_t save_minimal(t_archive&, const facade::t_duration& dur)
    {
        return dur.count();
    }

    template <class t_archive>
    void load_minimal(t_archive&, facade::t_duration& dur, const uint64_t& value)
    {
        dur = facade::t_duration{value};
    }

    template <class t_archive>
    void serialize(t_archive& archive, facade::function_call& call)
    {
        archive(cereal::make_nvp("function_name", call.function_name),
            cereal::make_nvp("pre_call_args", call.pre_call_args),
            cereal::make_nvp("results", call.results));
    }

    template <class t_archive>
    void serialize(t_archive& archive, facade::function_result& result)
    {
        archive(cereal::make_nvp("post_call_args", result.post_call_args),
            cereal::make_nvp("return_value", result.return_value),
            cereal::make_nvp("offest_from_origin", result.offest_from_origin),
            cereal::make_nvp("duration", result.duration));
    }
}  // namespace cereal

namespace facade
{
    using t_cereal_output_archive = cereal::JSONOutputArchive;
    using t_cereal_input_archive = cereal::JSONInputArchive;
    using t_hasher = digestpp::md5;

    template <typename t_function, typename t_overrider, typename t_filter>
    struct function_call_context
    {
        std::string function_name;
        bool static_function{false};
        t_function function;
        t_overrider overrider;
        t_filter filter;

        function_call_context(std::string _function_name, bool _static_function,
            t_function _function, t_overrider _overrider, t_filter _filter)
            : function_name(std::move(_function_name)),
              static_function(_static_function),
              function(std::move(_function)),
              overrider(std::move(_overrider)),
              filter(std::move(_filter))
        {
        }
    };

    template <typename t_archive>
    class arg_unpacker
    {
        const std::string& m_function_name;
        t_archive& m_archive;

    public:
        arg_unpacker(const std::string& function_name, t_archive& archive)
            : m_function_name(function_name), m_archive(archive)
        {
        }

        template <typename t_arg>
        void operator()(t_arg& arg)
        {
            try {
                if constexpr (std::is_const<t_arg>::value) {
                    // If argument has costant qualifier we extract it to a dummy
                    // variable to move further through the stream.
                    // Can be optimized by skipping the field in the stream
                    typename std::decay<t_arg>::type dummy;
                    m_archive(dummy);
                } else {
                    m_archive(arg);
                }
            } catch (std::exception& e) {
                master::get_instance().log_message(log_message_level::error,
                    std::string{"Failed to unpack an argument/return value of "} +
                        m_function_name + ", exception: " + e.what());
            } catch (...) {
                master::get_instance().log_message(log_message_level::error,
                    std::string{"Failed to unpack an argument/return value of "} +
                        m_function_name + ", unknown exception");
            }
        }
    };

    template <typename... t_args>
    void unpack(
        const std::string& function_name, const std::string& recorded, t_args&&... args)
    {
        if (recorded.empty()) return;
        std::stringstream ss;
        ss.str(recorded);
        t_cereal_input_archive archive{ss};
        arg_unpacker unpacker{function_name, archive};
        utils::visit_args(unpacker, std::forward<t_args>(args)...);
    }

    template <typename t_ret, typename... t_args>
    void unpack_callback(const function_call& this_call, std::any& any_ret,
        std::tuple<t_args...>& args_tuple)
    {
        const auto& callback_result = this_call.get_next_result(result_selection::once);
        std::apply(
            [&this_call](t_args&... args) {
                unpack(this_call.function_name, this_call.pre_call_args, args...);
            },
            args_tuple);

        constexpr const bool has_return = !std::is_same<t_ret, void>::value;
        if constexpr (has_return) {
            t_ret ret;
            unpack(this_call.function_name, callback_result.return_value, ret);
            any_ret = ret;
        }
    }

    template <typename t_ctx, typename t_ret, typename... t_args>
    void invoke_callback(t_ctx& ctx, const function_call& this_call)
    {
        if (!ctx.function) return;

        std::any any_ret;
        std::tuple<typename std::decay<t_args>::type...> pre_call_args_tuple;
        std::tuple<typename std::decay<t_args>::type...> post_call_args_tuple;

        unpack_callback<t_ret>(this_call, any_ret, pre_call_args_tuple);

        // override arguments
        if (ctx.overrider) std::apply(ctx.overrider, pre_call_args_tuple);

        constexpr const bool has_return = !std::is_same<t_ret, void>::value;
        if constexpr (has_return) {
            t_ret ret = std::apply(ctx.function, pre_call_args_tuple);
            // TODO: [CALLBACKS] check callback post call and return values
        } else {
            std::apply(ctx.function, pre_call_args_tuple);
        }
    }

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

    template <typename t_filter_func, typename... t_args>
    void filter_args_and_record(
        t_filter_func& filter, std::string& recorded, t_args&&... args)
    {
        // make a copy of arguments so we don't modify the source
        auto args_tuple = std::make_tuple(args...);
        // filter the copies
        std::apply(filter, args_tuple);
        // bind the outpur string with the filtered args
        auto call_record_args = [&recorded](
                                    typename std::decay<t_args>::type&... lambda_args) {
            record_args(recorded, std::forward<t_args>(lambda_args)...);
        };

        std::apply(call_record_args, args_tuple);
    }

    inline std::string calculate_hash(const std::string& data)
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
        result_selection m_selection{result_selection::cycle};
        bool m_is_registered{false};
        // clang-format on

        using t_lock_guard = std::lock_guard<decltype(m_mtx)>;
        using t_method_record_inserter = void(const std::string& method_name,
            std::string& pre_call_args, function_result&& result);

        bool is_playing() const { return master().is_playing(); }
        bool is_recording() const { return master().is_recording(); }
        bool is_passing_through() const { return master().is_passing_through(); }

        static bool is_overriding_arguments()
        {
            return master().is_overriding_arguments();
        }

        void facade_load(const std::filesystem::path& file) override
        {
            std::error_code ec;
            if (!std::filesystem::exists(file, ec)) {
                throw std::runtime_error{
                    std::string{"a recording file doesn't exist: "} + file.string()};
            }
            t_lock_guard lg(m_mtx);
            std::ifstream ifs(file);
            if (!ifs.is_open()) {
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

        void facade_clear() override
        {
            t_lock_guard lg(m_mtx);
            m_calls.clear();
            m_callbacks.clear();
        }

        const std::list<function_call>& get_callbacks() const override
        {
            return m_callbacks;
        }

        void invoke_callback(const function_call& callback) override
        {
            const auto it = m_callback_invokers.find(callback.function_name);
            // callback invoker for this callback is not found
            // TODO: should probably warn the client about that
            if (it == m_callback_invokers.end()) return;

            m_callback_invokers[callback.function_name](callback);
        }

        void internal_register()
        {
            if (m_is_registered) return;
            master().register_facade(this);
            m_is_registered = true;
        }

        void internal_unregister()
        {
            if (!m_is_registered) return;
            master().unregister_facade(this);
            m_is_registered = false;
        }

        facade_base(std::string name, bool register_on_construction)
            : m_name(std::move(name))
        {
            if (register_on_construction) internal_register();
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

        ~facade_base() { internal_unregister(); }
    };

    template <typename t_type>
    class facade : public facade_base
    {
    protected:
        t_type* m_impl{nullptr};

        template <typename... t_args>
        void restore_args(std::string& recorded, t_args&&... args)
        {
            std::stringstream ss{recorded};
            {
                t_cereal_input_archive archive{ss};
                utils::visit_args(archive, std::forward<t_args>(args)...);
            }
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        typename std::decay<t_ret>::type replay_function_call(
            t_ctx& ctx, t_args&&... args)
        {
            constexpr const bool has_return = !std::is_same<t_ret, void>::value;
            const auto method_it = m_calls.find(ctx.function_name);
            if (method_it == m_calls.end()) {
                if constexpr (has_return) {
                    return {};
                } else {
                    return;
                }
            }
            std::string pre_call_args;
            record_args(pre_call_args, std::forward<t_args>(args)...);
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
            std::this_thread::sleep_for(t_duration{this_method_call_result.duration});
            unpack(ctx.function_name, this_method_call_result.post_call_args,
                std::forward<t_args>(args)...);
            if constexpr (!has_return) {
                if (ctx.overrider) { ctx.overrider(std::forward<t_args>(args)...); }
            } else {
                typename std::decay<t_ret>::type ret{};
                unpack(ctx.function_name, this_method_call_result.return_value, ret);
                if (ctx.overrider) { ret = ctx.overrider(std::forward<t_args>(args)...); }
                return ret;
            }
        }

        void insert_method_call(const std::string& method_name,
            std::string& pre_call_args, function_result&& result)
        {
            const auto hash = calculate_hash(pre_call_args);
            t_lock_guard lg(m_mtx);
            auto& method_calls = m_calls[method_name];
            auto method_call_it = method_calls.find(hash);
            if (method_call_it == method_calls.end()) {
                function_call function_call;
                function_call.function_name = method_name;
                function_call.pre_call_args = std::move(pre_call_args);
                method_call_it =
                    method_calls.insert({hash, std::move(function_call)}).first;
            }

            method_call_it->second.results.emplace_back(std::move(result));
        }

        void insert_callback_call(const std::string& function_name,
            std::string& pre_call_args, function_result&& result)
        {
            t_lock_guard lg(m_mtx);
            function_call callback_call;
            callback_call.function_name = function_name;
            callback_call.pre_call_args = std::move(pre_call_args);
            callback_call.results.emplace_back(std::move(result));
            m_callbacks.emplace_back(std::move(callback_call));
        }

        template <typename t_ctx, typename... t_args>
        void record_args_with_filter(t_ctx& ctx, std::string& recorded, t_args&&... args)
        {
            if (!ctx.filter) {
                record_args(recorded, std::forward<t_args>(args)...);
                return;
            }

            ctx.filter(recorded, std::forward<t_args>(args)...);
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        typename std::decay<t_ret>::type call_function_and_record(t_ctx& ctx,
            const std::function<t_method_record_inserter>& inserter, t_args&&... args)
        {
            if (!ctx.static_function && !m_impl) {
                throw std::runtime_error{
                    std::string{"implementation is not set for "} + facade_name()};
            }
            std::string pre_call_args;
            record_args_with_filter(ctx, pre_call_args, std::forward<t_args>(args)...);
            function_result this_call_result;
            this_call_result.offest_from_origin = master().get_offset_from_origin();
            utils::timer timer;
            std::any ret;
            constexpr bool has_return = !std::is_same<t_ret, void>::value;
            if constexpr (has_return) {
                ret = ctx.function(std::forward<t_args>(args)...);
                record_args(this_call_result.return_value, std::any_cast<t_ret>(ret));
            } else {
                ctx.function(std::forward<t_args>(args)...);
            }
            this_call_result.duration = timer.get_duration<t_duration>();
            record_args_with_filter(
                ctx, this_call_result.post_call_args, std::forward<t_args>(args)...);
            inserter(ctx.function_name, pre_call_args, std::move(this_call_result));
            if constexpr (has_return) { return std::any_cast<t_ret>(ret); }
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        typename std::decay<t_ret>::type pass_through(t_ctx& ctx, t_args&&... args)
        {
            if (!ctx.static_function && !m_impl) {
                // if facade doesn't hold an implementation then just return
                constexpr const bool has_return = !std::is_same<t_ret, void>::value;
                if constexpr (has_return) {
                    return {};
                } else {
                    return;
                }
            }
            return ctx.function(std::forward<t_args>(args)...);
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        typename std::decay<t_ret>::type call_method(t_ctx& ctx, t_args&&... args)
        {
            if (is_playing()) {
                return replay_function_call<t_ret>(ctx, std::forward<t_args>(args)...);
            }
            if (is_recording()) {
                auto inserter = [this](const std::string& method_name,
                                    std::string& pre_call_args,
                                    function_result&& result) -> void {
                    insert_method_call(method_name, pre_call_args, std::move(result));
                };
                return call_function_and_record<t_ret>(
                    ctx, inserter, std::forward<t_args>(args)...);
            } else {
                return pass_through<t_ret>(ctx, std::forward<t_args>(args)...);
            }
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        typename std::decay<t_ret>::type call_callback(t_ctx& ctx, t_args&&... args)
        {
            if (is_playing()) {
                throw std::runtime_error(
                    "call_callback is not expected to be called during m_playing == "
                    "true");
            }
            if (is_recording()) {
                auto inserter = [this](const std::string& method_name,
                                    std::string& pre_call_args,
                                    function_result&& result) -> void {
                    insert_callback_call(method_name, pre_call_args, std::move(result));
                };
                return call_function_and_record<t_ret>(
                    ctx, inserter, std::forward<t_args>(args)...);
            } else {
                return pass_through<t_ret>(ctx, std::forward<t_args>(args)...);
            }
        }

        template <typename t_ret, typename t_ctx, typename... t_args>
        auto create_callback_wrapper(t_ctx& ctx)
        {
            return [this, ctx](t_args... args) -> t_ret {
                return call_callback<t_ret>(ctx, args...);
            };
        }

    public:
        using t_impl_type = t_type;
        using t_const_impl_type = typename std::add_const<t_type>::type;

        facade(std::string name, bool register_on_consturction)
            : facade_base(std::move(name), register_on_consturction)
        {
        }

        facade(std::string name) : facade_base(std::move(name), true) {}
    };
}  // namespace facade

#endif