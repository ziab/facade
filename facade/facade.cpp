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
};

class foo_facade : public facade::facade<foo>
{
public:
    FACADE_CONSTRUCTOR(foo_facade);
    FACADE_METHOD(do_stuff);
};

int main()
{
	std::cout << "Hello CMake." << std::endl;

    auto impl = std::make_unique<foo>();
    foo_facade foo{ std::move(impl) };

    foo.do_stuff(false, 3, "hello!");
    foo.do_stuff(true, 42, "hello again!");
	return 0;
}
