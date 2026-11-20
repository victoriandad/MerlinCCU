#include "text_utils.h"

#include <cstring>

#include "test_framework.h"

namespace
{

constexpr uint16_t kConfiguredHaPort = 8123U;
constexpr size_t kHostBufferSize = 48U;

struct ParsedHttpEndpoint
{
    bool ok;
    char host[kHostBufferSize];
    uint16_t port;
    bool tls;
};

ParsedHttpEndpoint parse_http(const char* text)
{
    ParsedHttpEndpoint out = {};
    out.ok = text_utils::parse_endpoint(text, text_utils::kHttpSchemes, kConfiguredHaPort,
                                        out.host, sizeof(out.host), &out.port, &out.tls);
    return out;
}

} // namespace

HOST_TEST(parse_endpoint_bare_host_uses_the_configured_port)
{
    const ParsedHttpEndpoint endpoint = parse_http("ha.lan");
    EXPECT_TRUE(endpoint.ok);
    EXPECT_EQ(std::strcmp(endpoint.host, "ha.lan"), 0);
    EXPECT_EQ(endpoint.port, kConfiguredHaPort);
    EXPECT_FALSE(endpoint.tls);
}

HOST_TEST(parse_endpoint_http_url_without_a_port_means_port_80)
{
    const ParsedHttpEndpoint endpoint = parse_http("http://ha.lan/");
    EXPECT_TRUE(endpoint.ok);
    EXPECT_EQ(std::strcmp(endpoint.host, "ha.lan"), 0);
    EXPECT_EQ(endpoint.port, text_utils::kHttpDefaultPort);
    EXPECT_FALSE(endpoint.tls);
}

HOST_TEST(parse_endpoint_https_url_without_a_port_means_port_443_with_tls)
{
    const ParsedHttpEndpoint endpoint = parse_http("https://ha.example.org");
    EXPECT_TRUE(endpoint.ok);
    EXPECT_EQ(std::strcmp(endpoint.host, "ha.example.org"), 0);
    EXPECT_EQ(endpoint.port, text_utils::kHttpsDefaultPort);
    EXPECT_TRUE(endpoint.tls);
}

HOST_TEST(parse_endpoint_explicit_port_wins_over_scheme_and_config)
{
    EXPECT_EQ(parse_http("http://homeassistant.local:8123").port, 8123U);
    EXPECT_EQ(parse_http("192.168.1.116:80").port, 80U);
    EXPECT_EQ(parse_http("https://ha.lan:8443/").port, 8443U);
}

HOST_TEST(parse_endpoint_reports_where_a_path_starts)
{
    char host[kHostBufferSize] = {};
    uint16_t port = 0U;
    const char* end = nullptr;
    EXPECT_TRUE(text_utils::parse_endpoint("http://ha.lan/api", text_utils::kHttpSchemes,
                                           kConfiguredHaPort, host, sizeof(host), &port, nullptr,
                                           &end));
    EXPECT_EQ(std::strcmp(end, "/api"), 0);
}

HOST_TEST(parse_endpoint_rejects_an_empty_host_or_a_bad_port)
{
    EXPECT_FALSE(parse_http("http://").ok);
    EXPECT_FALSE(parse_http("ha.lan:").ok);
    EXPECT_FALSE(parse_http("ha.lan:0").ok);
    EXPECT_FALSE(parse_http("ha.lan:70000").ok);
}

HOST_TEST(parse_endpoint_mqtt_schemes_mean_port_1883)
{
    constexpr uint16_t kConfiguredBrokerPort = 1884U;
    char host[kHostBufferSize] = {};
    uint16_t port = 0U;
    EXPECT_TRUE(text_utils::parse_endpoint("mqtt://ha.lan", text_utils::kMqttSchemes,
                                           kConfiguredBrokerPort, host, sizeof(host), &port));
    EXPECT_EQ(port, text_utils::kMqttDefaultPort);
    EXPECT_TRUE(text_utils::parse_endpoint("ha.lan", text_utils::kMqttSchemes,
                                           kConfiguredBrokerPort, host, sizeof(host), &port));
    EXPECT_EQ(port, kConfiguredBrokerPort);
}
