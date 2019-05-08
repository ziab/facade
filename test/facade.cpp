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

namespace example
{
    class network_interface
    {
        std::string m_ip;
        std::map<std::string, std::string> m_dns_cache;
    public:
        bool initialize()
        {
            m_ip = "192.168.1.31";
            m_dns_cache = { { "mail_server", "192.168.1.3" }, { "message_server", "192.168.1.12"} };
            return true;
        }
        const std::string& get_local_ip() const { return m_ip; }
        std::string resolve(const std::string& name)
        {
            if (m_dns_cache.find(name) != m_dns_cache.end()) return m_dns_cache[name];
            return "unresolved";
        }

        bool send(const std::string& address, const std::string& message, std::string& reply)
        {
            if (address == "192.168.1.3" || address == "192.168.1.12") {
                reply = "Your message: '" + message + "' is delivered";
                return true;
            }
            return false;
        }
    };

    class network_interface_facade : public facade::facade<network_interface>
    {
    public:
        FACADE_CONSTRUCTOR(network_interface_facade);
        FACADE_METHOD(initialize);
        FACADE_METHOD(get_local_ip);
        FACADE_METHOD(resolve);
        FACADE_METHOD(send);
    };

    void use_network(network_interface_facade& net)
    {
        std::cout << "Initializing network, result: " << net.initialize() << std::endl;
        std::cout << "Local IP: " << net.get_local_ip() << std::endl;
        const auto mail_server_ip = net.resolve(std::string{ "mail_server" });
        const auto message_server_ip = net.resolve(std::string{ "message_server" });
        const auto storage_server_ip = net.resolve(std::string{ "storage_server" });
        std::cout << "mail_server_ip = " << mail_server_ip << ", message_server_ip = " << message_server_ip
            << ", storage_server_ip = " << storage_server_ip << std::endl;
        std::string reply;
        auto result = net.send(mail_server_ip, std::string{ "Hello mail server!" }, reply);
        if (result) std::cout << "Received reply from the mail server: " << reply << std::endl;
        reply.clear();
        result = net.send(mail_server_ip, std::string{ "Hello message server!" }, reply);
        if (result) std::cout << "Received reply from the message server: " << reply << std::endl;
    }

    void run()
    {
        {
            auto net_impl = std::make_unique<network_interface>();
            network_interface_facade net{ std::move(net_impl), true };
            use_network(net);
            net.write_calls("network_interface.json");
        }
        {
            utils::print_json("network_interface.json");
            network_interface_facade net{ "network_interface.json" };
            use_network(net);
        }
    }
}

namespace example2
{
    class foo
    {
        const bool expected_param1{ true };
        const int expected_param2{ 42 };
    public:
        foo() {};

        void no_input_no_return_function() {}
        int no_input_function() { return 100500; }
        std::string another_no_input_function() const { return "100500"; }
        bool input_output_function(bool param1, int param2, std::string& output)
        {
            if (param1 == expected_param1 && expected_param2 == 42) {
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
        FACADE_METHOD(another_no_input_function);
        FACADE_METHOD(input_output_function);
        FACADE_METHOD(template_function);
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

    void run()
    {
        record();
        play();
    }
}

int main()
{
    example::run();
    example2::run();
	return 0;
}
