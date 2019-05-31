# facade - a C++17 library _under development_ for mocking C++ class implementation
[![Build status](https://ci.appveyor.com/api/projects/status/77ubk71of7reap9c?svg=true)](https://ci.appveyor.com/project/ziab/facade)
[![Build Status](https://travis-ci.com/ziab/facade.svg?branch=master)](https://travis-ci.com/ziab/facade)

`facade` is a header-only C++17 library **_under development_** for mocking implementation of C++ classes.
Mocking is perfomed by creating a facade (a wrapper) for the original implementation, recording all typical calls made to it and storing that information as a database file. Without instantiating the original implementation, the database file can later be loaded and "replayed".

Imagine you have a class that represents a network interface:
```cpp
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
```
And the class may be used like this:
```cpp
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
```
Quite often you run into a situation where you would like to test a client of such class but using the full-blown implementation of it might not be feasible in the test environment. In case of a real network interface, the servers that the client communicates with might not be available in the test environment. This is where `facade` is intended to help.
Now in order to create a facade you declare a class derived from `facade::facade<T>`:
```cpp
class network_interface_facade : public facade::facade<network_interface>
{
public:
    FACADE_CONSTRUCTOR(network_interface_facade);
    FACADE_METHOD(initialize);
    FACADE_METHOD(get_local_ip);
    FACADE_METHOD(resolve);
    FACADE_METHOD(send);
};
```
* `FACADE_CONSTRUCTOR` declares pre-defined constructors of the class, one for initializing it with a pointer to the original implementation and another one for providing a path to a file with a recorded database.
* `FACADE_METHOD` expands into a "trampoline" function that captures the details of the method call, i.e. method name, arguments before and after the call, return value
  * The method *does not* have to be virtual, at the current state there are no strict requirements, the plan is to support any kind of member function: non-const, const, virtual, non-virtual, template, static
* `FACADE_CALLBACK` exapands into a "trempoline" function and a callback registration function, more information on this will be added later
  
Then you create a recording of `network_interface`'s behavior:
```cpp
void run()
{
    {
        facade::master().start_recording();
        auto net_impl = std::make_unique<network_interface>();
        network_interface_facade net{std::move(net_impl)};
        use_network(net);
    }
    {
        facade::master().start_playing();
        network_interface_facade net;
        use_network(net);
    }
}
```

The first block creates a "recording" facade of `network_interface`, each call to it is recroded into an internal database-like structure. `net.write_calls("network_interface.json")` saves the database into a JSON file that can be later loaded, example:

```json
{
    "name": "network_interface_facade",
    "calls": [
        {
            "key": "resolve",
            "value": [
                {
                    "key": "c6aaf60e13481e22021ff4997cd5215c",
                    "value": {
                        "pre_args": "{\n    \"value0\": \"mail_server\"\n}",
                        "post_args": "{\n    \"value0\": \"mail_server\"\n}",
                        "ret": "{\n    \"value0\": \"192.168.1.3\"\n}",
                        "duration": 77
                    }
                },
                {
                    "key": "f08127ab0fa520dae14306dd38feb0c2",
                    "value": {
                        "pre_args": "{\n    \"value0\": \"message_server\"\n}",
                        "post_args": "{\n    \"value0\": \"message_server\"\n}",
                        "ret": "{\n    \"value0\": \"192.168.1.12\"\n}",
                        "duration": 60
                    }
                },
                {
                    "key": "cad241558459355b48bf1706121fb501",
                    "value": {
                        "pre_args": "{\n    \"value0\": \"storage_server\"\n}",
                        "post_args": "{\n    \"value0\": \"storage_server\"\n}",
                        "ret": "{\n    \"value0\": \"unresolved\"\n}",
                        "duration": 71
                    }
                }
            ]
        },
        ...
```

`network_interface_facade net{ "network_interface.json" };` creates a "replaying" facade that will use data from the JSON file. Every call(a call is defined by a unique combination of input parameters) that has been recorded before will be "replayable", meaning that the return value and out parameters will be initialized with what they were during recording.

Check this [example](example/example.cpp) or [unit test folder](facade_test) for more information

Credits:
* [cereal](https://github.com/USCiLab/cereal)
* [digestpp](https://github.com/kerukuro/digestpp)
