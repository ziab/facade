#include "facade.h"

#include <iostream>

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

int main(int, char**)
{
    example::run();
}
