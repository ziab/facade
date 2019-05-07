#include "facade.h"

#include <iostream>
#include <string>

namespace utils
{
    void print_json(const std::filesystem::path& path)
    {
        std::ifstream ifs{ path };
        while (ifs.good())
        {
            char c;
            ifs.read(&c, 1);
            std::cout << c;
        }
        std::cout << std::endl;
    }
}

class foo
{
    const bool expected_param1{ true} ;
    const int expected_param2{ 42 };
public:
    foo() {};
    int do_stuff(bool param1, int param2, const std::string& param3)
    {
        if (param1 == expected_param1 && expected_param2 == 42) return 1;
        return 0;
    }

    int no_input_function()
    {
        return 100500;
    }

    template <typename T>
    std::string template_function(T val)
    {
        std::stringstream ss;
        ss << val;
        return ss.str();
    }

    void no_input_no_return_function()
    {
    }
};

class foo_facade : public facade::facade<foo>
{
public:
    FACADE_CONSTRUCTOR(foo_facade);
    FACADE_METHOD(do_stuff);
    FACADE_METHOD(no_input_function);
    FACADE_METHOD(no_input_no_return_function);
    FACADE_TEMPLATE_METHOD(template_function);
};

void record()
{
    auto impl = std::make_unique<foo>();
    foo_facade foo{ std::move(impl), true };

    foo.do_stuff(false, 3, std::string{ "hello!" });
    foo.do_stuff(true, 42, std::string{ "hello again!" });
    foo.no_input_function();
    foo.no_input_no_return_function();
    foo.template_function<int>(100);

    foo.write_calls("calls.json");
    utils::print_json("calls.json");
}

void play()
{
    foo_facade foo{ "calls.json" };

    std::cout << "foo.do_stuff returned: " << foo.do_stuff(false, 3, std::string{ "hello!" }) << std::endl;
    std::cout << "foo.do_stuff returned: " << foo.do_stuff(true, 42, std::string{ "hello again!" }) << std::endl;
    std::cout << "foo.no_input_function() returned: " << foo.no_input_function() << std::endl;
    foo.no_input_no_return_function();
    std::cout << "foo.template_function returned: " << foo.template_function<int>(100) << std::endl;  
}

int main()
{
    record();
    play();
	return 0;
}
