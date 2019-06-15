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
    class a_class
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
        a_class(){};

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
                return true;
            }
            output = "No data";
            return false;
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

    class a_class_facade : public facade::facade<a_class>
    {
    public:
        FACADE_CONSTRUCTOR(a_class_facade);
        FACADE_METHOD(no_input_no_return_function);
        FACADE_METHOD(no_input_function);
        FACADE_METHOD(const_no_input_function);
        FACADE_METHOD(input_output_function);
        FACADE_METHOD(template_function);
        FACADE_CALLBACK(input_function_cbk, void, bool, int);
        FACADE_CALLBACK(input_output_function_cbk, bool, bool, int, std::string&);
        FACADE_CALLBACK(no_input_function_cbk, void);
    };

    void test_exceptions(test_classes::a_class_facade& facade)
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

void compare_result(test_classes::a_class_facade& facade, test_classes::a_class& original)
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

    facade::master().wait_all_pending_callbacks_replayed();

    ASSERT_EQ(test_classes::g_input_callback_times_called, 2);
    ASSERT_EQ(test_classes::g_input_output_callback_times_called, 1);
    ASSERT_EQ(test_classes::g_no_input_callback_times_called, 1);
}

TEST(basic, compare_results)
{
    using namespace test_classes;
    {
        facade::master().set_number_of_workers(1);
        utils::delete_recording<a_class_facade>();
        facade::master().start_recording();
        // Compare recording facade with the original implementation
        auto impl = std::make_unique<a_class>();
        a_class_facade facade{std::move(impl)};

        facade.rewire_callbacks([](a_class& impl, a_class_facade& facade) {
            facade.register_callback_input_function_cbk(input_callback);
            facade.register_callback_input_output_function_cbk(input_output_callback);
            facade.register_callback_no_input_function_cbk(no_input_callback);

            impl.register_input_function_cbk(facade.get_callback_input_function_cbk());
            impl.register_input_output_function_cbk(
                facade.get_callback_input_output_function_cbk());
            impl.register_no_input_function_cbk(
                facade.get_callback_no_input_function_cbk());
        });

        test_classes::a_class original;
        compare_result(facade, original);
    }
    {
        // Compare replaying facade with the original implementation
        facade::master().start_playing();
        test_classes::a_class_facade facade;
        test_classes::a_class original;

        facade.register_callback_input_function_cbk(input_callback);
        facade.register_callback_input_output_function_cbk(input_output_callback);
        facade.register_callback_no_input_function_cbk(no_input_callback);

        compare_result(facade, original);
        test_exceptions(facade);
        utils::print_json(facade::master().make_recording_path(facade));
        facade::master().wait_all_pending_callbacks_replayed();
        facade::master().stop();
    }
}

namespace test_classes
{
    class singleton
    {
        const bool m_expected_param1{true};
        const int m_expected_param2{42};

        singleton() {}
        ~singleton() {}

    public:
        static int no_input_function() { return 100500; }
        std::string const_no_input_function(int val) const { return "100500"; }
        static bool input_output_function(bool param1, int param2, std::string& output)
        {
            if (param1 == get_singleton().m_expected_param1 &&
                param2 == get_singleton().m_expected_param2) {
                output = "There is some data";
                return true;
            }
            output = "No data";
            return false;
        }

        static bool function_to_override(bool param1, int param2, std::string& output)
        {
            output = "original";
            return false;
        }

        static singleton& get_singleton()
        {
            static singleton instance;
            return instance;
        }
    };

    class singleton_facade : public facade::facade<singleton>
    {
    public:
        FACADE_SINGLETON_CONSTRUCTOR(singleton_facade);
        FACADE_STATIC_METHOD(no_input_function);
        FACADE_METHOD(const_no_input_function);
        FACADE_STATIC_METHOD(input_output_function);

        FACADE_STATIC_METHOD(function_to_override);
        static bool override_function_to_override(
            bool param1, int param2, std::string& output)
        {
            output = "overridden";
            return false;
        }
    };
}  // namespace test_classes

void compare_result(
    test_classes::singleton_facade& facade, test_classes::singleton& original)
{
    namespace t = test_classes;
    ASSERT_EQ(
        t::singleton_facade::no_input_function(), t::singleton::no_input_function());

    do_compare_results(facade, original, const_no_input_function, 0);

    std::string a_string, b_string;
    ASSERT_EQ(t::singleton_facade::input_output_function(false, 3, a_string),
        t::singleton::input_output_function(false, 3, b_string));
    ASSERT_EQ(a_string, b_string);
    a_string.clear();
    b_string.clear();
    ASSERT_EQ(facade.input_output_function(true, 42, a_string),
        original.input_output_function(true, 42, b_string));
    ASSERT_EQ(a_string, b_string);
    a_string.clear();
    b_string.clear();

    // just call the function to record the output, don't compare the results because they
    // should be different
    facade.function_to_override(1, 42, a_string);
    a_string.clear();
}

void check_overrider(test_classes::singleton_facade& facade)
{
    std::string str;
    facade.function_to_override(1, 42, str);
    ASSERT_EQ(str, "overridden") << "function call parameter was not overridden";
}

TEST(singleton, compare_results)
{
    using namespace test_classes;
    const auto facade_recording_file =
        facade::master().make_recording_path(singleton_facade::get_facade_instance());

    std::error_code ec;
    std::filesystem::remove(facade_recording_file, ec);

    {
        facade::master().set_number_of_workers(1);
        facade::master().start_recording();
        // Compare recording facade with the original implementation
        auto& impl = singleton::get_singleton();
        auto& singleton_facade_inst = singleton_facade::get_facade_instance();
        singleton_facade_inst.set_impl(&impl);
        singleton_facade_inst.register_facade();

        auto& original = singleton::get_singleton();
        compare_result(singleton_facade_inst, original);

        singleton_facade_inst.unregister_facade();
    }
    {
        // Compare replaying facade with the original implementation
        facade::master().start_playing();
        auto& impl = singleton::get_singleton();
        auto& singleton_facade_inst = singleton_facade::get_facade_instance();

        singleton_facade_inst.register_facade();

        compare_result(singleton_facade_inst, impl);
        check_overrider(singleton_facade_inst);
        // utils::print_json(facade_recording_file);
        singleton_facade_inst.unregister_facade();
        facade::master().stop();
    }
}

namespace test_overrider
{
    class a_class
    {
    public:
        using t_cbk = bool(bool param1, int param2, const std::string& param3);

    private:
        const bool m_expected_param1{true};
        const int m_expected_param2{42};

        std::function<t_cbk> m_cbk;

    public:
        a_class(){};

        int input_output_function(
            const bool param1, const int param2, std::string& output)
        {
            // modify parameters so we can test the overrider function later
            if (m_cbk) m_cbk(!param1, param2 * 2, "original");

            if (param1 == m_expected_param1 && param2 == m_expected_param2) {
                output = "There is some data";
                return 1;
            }
            output = "No data";
            return 0;
        }

        void register_callback(const std::function<t_cbk>& cbk) { m_cbk = cbk; }
    };

    class a_class_facade : public facade::facade<a_class>
    {
    public:
        FACADE_CONSTRUCTOR(a_class_facade);

        FACADE_METHOD(input_output_function);
        int override_input_output_function(
            const bool param1, const int param2, std::string& output)
        {
            output = "There is some data overriden";
            return 2;
        }

        FACADE_CALLBACK(callback, bool, const bool, const int, const std::string&);
        bool override_callback(bool& param1, int& param2, std::string& param3)
        {
            param1 = !param1;
            param2 /= 2;
            param3 = "overridden";
            return true;
        }
    };

    bool g_recording = false;
    bool g_callback_test_is_ok = true;

    bool callback(bool param1, int param2, const std::string& param3)
    {
        if (!g_recording) {
            if (param1 != true || param2 != 42 || param3 != "overridden") {
                g_callback_test_is_ok = false;
            }
        }
        return true;
    }

}  // namespace test_overrider

void use(test_overrider::a_class_facade& facade)
{
    namespace t = test_overrider;
    std::string str;
    facade.input_output_function(true, 42, str);
}

void check(test_overrider::a_class_facade& facade)
{
    namespace t = test_overrider;
    std::string str;
    auto val = facade.input_output_function(true, 42, str);
    ASSERT_EQ(str, "There is some data overriden")
        << "function call parameter was not overridden";
    ASSERT_EQ(val, 2) << "return value was not overridden";
}

TEST(overrider, basic)
{
    using namespace test_overrider;
    {
        facade::master().set_number_of_workers(1);
        utils::delete_recording<a_class_facade>();

        test_overrider::g_recording = true;
        facade::master().start_recording();

        a_class_facade facade{std::make_unique<a_class>()};

        facade.rewire_callbacks([](a_class& impl, a_class_facade& facade) {
            impl.register_callback(facade.get_callback_callback());
            facade.register_callback_callback(callback);
        });

        use(facade);
    }
    {
        test_overrider::g_recording = false;
        facade::master().start_playing();
        a_class_facade facade;
        facade.register_callback_callback(callback);

        check(facade);

        facade::master().wait_all_pending_callbacks_replayed();
        facade::master().stop();
        ASSERT_TRUE(g_callback_test_is_ok) << "callback parameters were not overridden";
    }
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
