#include "facade.h"

#include <iostream>
#include <string>

class foo
{
public:
    foo() {};
    int do_stuff(bool param1, int param2, const std::string& param3)
    {
        if (param1 == true && param2 == 42) return 1;
        return 0;
    }

    bool no_input_function()
    {
        return true;
    }
};

class foo_facade : public facade::facade<foo>
{
public:
    FACADE_CONSTRUCTOR(foo_facade);
    FACADE_METHOD(do_stuff);
    FACADE_METHOD(no_input_function);
};

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

int main()
{
    auto impl = std::make_unique<foo>();
    foo_facade foo{ std::move(impl) };

    foo.do_stuff(false, 3, std::string{ "hello!" });
    foo.do_stuff(true, 42, std::string{ "hello again!" });
    foo.no_input_function();

    foo.write_calls("calls.json");
    print_json("calls.json");
    system("pause");
	return 0;
}
