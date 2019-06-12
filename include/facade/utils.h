#pragma once
#include <chrono>
#include <iostream>

namespace facade
{
    using t_duration = std::chrono::microseconds;
    using t_highres_timepoint =
        std::chrono::time_point<std::chrono::high_resolution_clock>;

    namespace utils
    {
        namespace traits
        {
            // clang-format off
            template<class t_type>
            struct is_pointer_to_const_member_function : std::false_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args...) const> : std::true_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args...) const &> : std::true_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args...) const &&> : std::true_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args..., ...) const> : std::true_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args..., ...) const &> : std::true_type {};

            template<class t_ret, class t_type, class... t_args>
            struct is_pointer_to_const_member_function<t_ret(t_type::*)(t_args..., ...) const &&> : std::true_type {};
            // clang-format on
        }  // namespace traits

        class timer
        {
            std::chrono::time_point<std::chrono::system_clock> m_time_started;

        public:
            timer(const timer&) = delete;
            timer& operator=(const timer&) = delete;
            timer& operator=(timer&& that) noexcept
            {
                m_time_started = std::move(that.m_time_started);
                return *this;
            }

            timer() { m_time_started = std::chrono::system_clock::now(); }

            template <typename t_duration>
            t_duration get_duration() const
            {
                const auto now = std::chrono::system_clock::now();
                return std::chrono::duration_cast<t_duration>(now - m_time_started);
            }
        };

        inline auto get_offset_from_origin(const t_highres_timepoint& origin)
        {
            return std::chrono::duration_cast<t_duration>(
                std::chrono::high_resolution_clock::now() - origin);
        }

        inline void sleep_until(
            const t_highres_timepoint& origin,
            const t_duration& that_offset_form_origin)
        {
            const auto this_offset = get_offset_from_origin(origin);
            const auto diff = that_offset_form_origin - this_offset;
            if (diff < t_duration::zero()) return;
            std::this_thread::sleep_for(diff);
        }

        template <typename t_visitor>
        inline void visit_args_impl(t_visitor&)
        {
        }

        template <typename t_visitor, typename t, typename... t_args>
        void visit_args_impl(t_visitor& visitor, t&& head, t_args&&... tail)
        {
            visitor(head);
            visit_args_impl(visitor, tail...);
        }

        template <typename t_visitor, typename... t_args>
        void visit_args(t_visitor& visitor, t_args&&... args)
        {
            visit_args_impl(visitor, std::forward<t_args>(args)...);
        }

        struct type_printer
        {
            template <typename t_arg>
            void operator()(const t_arg& arg)
            {
                std::cout << typeid(t_arg).name() << std::endl;
            }
        };

        template <typename... t_args>
        void print_arg_types(t_args&&... args)
        {
            type_printer tp;
            visit_args(tp, std::forward<t_args>(args)...);
        }

        template <typename t_input_type>
        struct get_type
        {
            using type = t_input_type;
        };
    }  // namespace utils
}  // namespace facade

// Check for any member function with given name
#define FACADE_CREATE_MEMBER_CHECK(_NAME)                                               \
    template <typename t_class, typename enabled = void>                                \
    struct has_member_##_NAME                                                           \
    {                                                                                   \
        static constexpr bool value = false;                                            \
    };                                                                                  \
    template <typename t_class>                                                         \
    struct has_member_##_NAME<t_class,                                                  \
        std::enable_if_t<std::is_member_function_pointer_v<decltype(&t_class::_NAME)>>> \
    {                                                                                   \
        static constexpr bool value =                                                   \
            std::is_member_function_pointer_v<decltype(&t_class::_NAME)>;               \
    };

#define FACADE_HAS_MEMBER(_TYPE, _NAME) has_member_##_NAME<_TYPE>::value
