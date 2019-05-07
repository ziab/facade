#include <chrono>

namespace facade
{
    namespace utils
    {
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
    }
}