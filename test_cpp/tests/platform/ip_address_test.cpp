#include <boost/test/unit_test.hpp>

#include <platform/np_ip_address.h>

#include <boost/asio.hpp>

BOOST_AUTO_TEST_SUITE(ip_address)

BOOST_AUTO_TEST_CASE(assign_ipv4)
{
    struct np_ip_address ip;
    np_ip_address_assign_v4(&ip, 2130706433 /*127.0.0.1*/);

    const char* str = np_ip_address_to_string(&ip);
    BOOST_TEST(std::string(str) == "127.0.0.1");
}

BOOST_AUTO_TEST_CASE(is_v4_mapped)
{
    struct np_ip_address ip;
    np_ip_address_assign_v4(&ip, 2130706433 /*127.0.0.1*/);

    struct np_ip_address v6;
    np_ip_convert_v4_to_v4_mapped(&ip, &v6);

    BOOST_TEST(np_ip_is_v4_mapped(&v6));

    const char* str = np_ip_address_to_string(&v6);
    BOOST_TEST(std::string(str) == "0000:0000:0000:0000:0000:ffff:7f00:0001");
}

BOOST_AUTO_TEST_CASE(is_v4)
{
    struct np_ip_address ip;
    ip.type = NABTO_IPV4;
    BOOST_TEST(np_ip_is_v4(&ip));
}

BOOST_AUTO_TEST_CASE(is_v6)
{
    struct np_ip_address ip;
    ip.type = NABTO_IPV6;
    BOOST_TEST(np_ip_is_v6(&ip));
}

BOOST_AUTO_TEST_CASE(read_v4)
{
    std::vector<std::string> ips = { "1.2.3.4", "123.255.0.0", "0.0.0.0" };

    for (auto ip : ips) {
        struct np_ip_address address;
        BOOST_TEST(np_ip_address_read_v4(ip.c_str(), &address));
        BOOST_TEST(std::string(np_ip_address_to_string(&address)) == ip);
    }
}

BOOST_AUTO_TEST_SUITE_END()
