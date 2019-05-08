#include <chrono>

namespace facade
{
    namespace utils
    {
        namespace traits
        {
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
        }

        class timer
        {
            std::chrono::time_point<std::chrono::system_clock> m_time_started;

        public:

            timer(const timer&) = delete;
            timer& operator= (const timer&) = delete;
            timer& operator=(timer&& that) noexcept
            {
                m_time_started = std::move(that.m_time_started);
                return *this;
            }

            timer()
            {
                m_time_started = std::chrono::system_clock::now();
            }

            template <typename t_duration = t_duration_resolution>
            uint64_t get_duration() const
            {
                const auto now = std::chrono::system_clock::now();
                const auto duration = std::chrono::duration_cast<t_duration>(now - m_time_started);
                return duration.count();
            }
        };

        template <typename t_visitor>
        inline void visit_args_impl(t_visitor&)
        {
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

        struct type_printer
        {
            template<typename t_arg>
            void operator()(const t_arg& arg)
            {
                std::cout << typeid(t_arg).name() << std::endl;
            }
        };

        template<typename ...t_args>
        void print_arg_types(t_args&& ...args)
        {
            type_printer tp;
            visit_args(tp, std::forward<t_args>(args)...);
        }
    }
}