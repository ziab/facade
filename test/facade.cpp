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

    void no_input_no_return_function() {}
    int no_input_function() { return 100500; }
    bool input_output_function(bool param1, int param2, std::string& output)
    {
        if (param1 == expected_param1 && expected_param2 == 42){
            output = "There is some data";
            return 1;
        }
        output = "No data";
        return 0;
    }
    template <typename T1, typename T2>
    std::string template_function(T1 t1, T2 t2)
    {
        return std::string{ "template_function: " }
            +typeid(t1).name() + " " + typeid(t2).name();
    }
};

class foo_facade : public facade::facade<foo>
{
public:
    FACADE_CONSTRUCTOR(foo_facade);
    FACADE_METHOD(no_input_no_return_function);
    FACADE_METHOD(no_input_function);
    FACADE_METHOD(input_output_function);
    FACADE_TEMPLATE_METHOD(template_function);
};

void record()
{
    auto impl = std::make_unique<foo>();
    foo_facade foo{ std::move(impl), true };

    foo.no_input_no_return_function();
    foo.no_input_function();
    foo.input_output_function(false, 3, std::string{});
    foo.input_output_function(true, 42, std::string{});
    foo.template_function<int, float>(100, 500.f);

    foo.write_calls("calls.json");
    utils::print_json("calls.json");
}

void play()
{
    foo_facade foo{ "calls.json" };

    foo.no_input_no_return_function();
    std::cout << "foo.no_input_function() returned: " << foo.no_input_function() << std::endl;
    std::string output;
    std::cout << "foo.no_input_function returned: " << foo.input_output_function(false, 3, output) << " output: " << output << std::endl;
    output.clear();
    std::cout << "foo.no_input_function returned: " << foo.input_output_function(true, 42, output) << " output: " << output << std::endl;
    std::cout << "foo.template_function returned: " << foo.template_function<int, float>(100, 500.f) << std::endl;
}

int main()
{
    record();
    play();
	return 0;
}
