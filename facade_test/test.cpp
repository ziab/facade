#include "facade.h"

#include <iostream>

#include <gtest/gtest.h>

namespace utils
{
    void print_json(const std::filesystem::path& path)
    {
        std::ifstream ifs{path};
        while (ifs.good()) {
            char c;
            ifs.read(&c, 1);
            std::cout << c;
        }
        std::cout << std::endl;
    }

    template <typename t_facade>
    void delete_recording()
    {
        t_facade facade;
        const auto facade_recording_file = facade::master().make_recording_path(facade);
        std::error_code ec;
        std::filesystem::remove(facade_recording_file, ec);
    }
}  // namespace utils

namespace test_classes
{
    class foo
    {
    public:
        using t_input_function_cbk = void(bool param1, int param2);
        using t_input_output_function_cbk = bool(
            bool param1, int param2, std::string& output);
        using t_no_input_function_cbk = void();

    private:
        const bool m_expected_param1{true};
        const int m_expected_param2{42};

        std::function<t_input_function_cbk> m_input_function_cbk{nullptr};
        std::function<t_input_output_function_cbk> m_input_output_function_cbk{nullptr};
        std::function<t_no_input_function_cbk> m_no_input_function_cbk{nullptr};

    public:
        foo(){};

        void no_input_no_return_function() 
        {
            if (m_no_input_function_cbk) m_no_input_function_cbk();
        }
        int no_input_function() { return 100500; }
        std::string const_no_input_function() const { return "100500"; }
        bool input_output_function(bool param1, int param2, std::string& output)
        {
            // use callback
            if (m_input_output_function_cbk) m_input_function_cbk(param1, param2);

            if (param1 == m_expected_param1 && param2 == m_expected_param2) {
                output = "There is some data";
                if (m_input_output_function_cbk) {
                    m_input_output_function_cbk(param1, param2, output);
                }
                return 1;
            }
            output = "No data";
            return 0;
        }
        template <typename T1, typename T2>
        std::string template_function(T1 t1, T2 t2)
        {
            return std::string{"template_function: "} + typeid(t1).name() + " " +
                typeid(t2).name();
        }

        void register_input_function_cbk(const std::function<t_input_function_cbk>& cbk)
        {
            m_input_function_cbk = cbk;
        }

        void register_input_output_function_cbk(
            const std::function<t_input_output_function_cbk>& cbk)
        {
            m_input_output_function_cbk = cbk;
        }

        void register_no_input_function_cbk(
            const std::function<t_no_input_function_cbk>& cbk)
        {
            m_no_input_function_cbk = cbk;
        }
    };

    class foo_facade : public facade::facade<foo>
    {
    public:
        FACADE_CONSTRUCTOR(foo_facade);
        FACADE_METHOD(no_input_no_return_function);
        FACADE_METHOD(no_input_function);
        FACADE_METHOD(const_no_input_function);
        FACADE_METHOD(input_output_function);
        FACADE_METHOD(template_function);
        FACADE_CALLBACK(input_function_cbk, void, bool, int);
        FACADE_CALLBACK(input_output_function_cbk, bool, bool, int, std::string&);
        FACADE_CALLBACK(no_input_function_cbk, void);
    };

    void test_exceptions(test_classes::foo_facade& facade)
    {
        facade.input_output_function(true, 43, std::string{});
    }

    size_t g_input_callback_times_called = 0;

    void input_callback(bool param1, int param2)
    {
        ++g_input_callback_times_called;
        std::cout << "input_callback is called with " << param1 << " " << param2
                  << std::endl;

        if (g_input_callback_times_called == 1) {
            ASSERT_EQ(param1, false);
            ASSERT_EQ(param2, 3);
        } else if (g_input_callback_times_called == 2) {
            ASSERT_EQ(param1, true);
            ASSERT_EQ(param2, 42);
        }
    }

    size_t g_input_output_callback_times_called = 0;

    bool input_output_callback(bool param1, int param2, std::string& output)
    {
        ++g_input_output_callback_times_called;
        std::cout << "input_output_callback is called with " << param1 << " " << param2
                  << " " << output << std::endl;

        if (param1 != true) throw std::logic_error("input_output_callback test failed");
        if (param2 != 42) throw std::logic_error("input_output_callback test failed");
        if (output != "There is some data") {
            throw std::logic_error("input_output_callback test failed");
        }

        return true;
    }

    size_t g_no_input_callback_times_called = 0;

    void no_input_callback()
    {
        ++g_no_input_callback_times_called;
        std::cout << "no_input_callback is called" << std::endl;
    }
}  // namespace test_classes

#define do_compare_results(A, B, method, ...)               \
    ASSERT_EQ(A.method(__VA_ARGS__), B.method(__VA_ARGS__)) \
        << #method " result mismatched";

void compare_foo_result(test_classes::foo_facade& facade, test_classes::foo& original)
{
    test_classes::g_input_callback_times_called = 0;
    test_classes::g_input_output_callback_times_called = 0;
    test_classes::g_no_input_callback_times_called = 0;

    facade.no_input_no_return_function();

    do_compare_results(facade, original, no_input_function);
    do_compare_results(facade, original, const_no_input_function);

    std::string a_string, b_string;
    ASSERT_EQ(facade.input_output_function(false, 3, a_string),
        original.input_output_function(false, 3, b_string));
    ASSERT_EQ(a_string, b_string);
    a_string.clear();
    b_string.clear();
    ASSERT_EQ(facade.input_output_function(true, 42, a_string),
        original.input_output_function(true, 42, b_string));
    ASSERT_EQ(a_string, b_string);
    a_string.clear();
    b_string.clear();

    do_compare_results(facade, original, template_function, 100, 500.f);

    // FIXME: this is workaround needed because there is no
    // way yo make sure all callbacks have been replayed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_EQ(test_classes::g_input_callback_times_called, 2);
    ASSERT_EQ(test_classes::g_input_output_callback_times_called, 1);
    ASSERT_EQ(test_classes::g_no_input_callback_times_called, 1);
}

TEST(basic, compare_results)
{
    using namespace test_classes;
    {
        utils::delete_recording<foo_facade>();
        facade::master().start_recording();
        // Compare recording facade with the original implementation
        auto impl = std::make_unique<foo>();
        foo_facade facade{std::move(impl)};

        facade.rewire_callbacks([](foo& impl, foo_facade& facade) {
            facade.register_callback_input_function_cbk(input_callback);
            facade.register_callback_input_output_function_cbk(input_output_callback);
            facade.register_callback_no_input_function_cbk(no_input_callback);

            impl.register_input_function_cbk(facade.get_callback_input_function_cbk());
            impl.register_input_output_function_cbk(
                facade.get_callback_input_output_function_cbk());
            impl.register_no_input_function_cbk(
                facade.get_callback_no_input_function_cbk());
        });

        test_classes::foo original;
        compare_foo_result(facade, original);
    }
    {
        // Compare replaying facade with the original implementation
        facade::master().start_playing();
        test_classes::foo_facade facade;
        test_classes::foo original;

        facade.register_callback_input_function_cbk(input_callback);
        facade.register_callback_input_output_function_cbk(input_output_callback);
        facade.register_callback_no_input_function_cbk(no_input_callback);

        compare_foo_result(facade, original);
        test_exceptions(facade);
        utils::print_json(facade::master().make_recording_path(facade));
    }
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
