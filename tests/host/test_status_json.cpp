#include "status_json.h"

#include <cstring>
#include <string>

#include "test_framework.h"

namespace
{

status_json::Status sample()
{
    status_json::Status status = {};
    status.unit = "merlinccu2";
    status.label = "Merlin CCU (Dev)";
    status.firmware = "cc3953d";
    status.board = "pico2_w";
    status.uptime_s = 3725U;
    status.wifi_connected = true;
    status.rssi_dbm = -61;
    status.ha_connected = true;
    status.mqtt_connected = false;
    status.free_heap = 12288U;
    status.current_page = "HOME";
    status.alerts_active = 2U;
    status.top_alert = "HEAP LOW";
    status.units_online = 1U;
    status.units_known = 2U;
    return status;
}

} // namespace

HOST_TEST(status_json_reports_every_field)
{
    char out[status_json::kMaxBytes] = {};
    EXPECT_TRUE(status_json::build(sample(), out, sizeof(out)));
    EXPECT_TRUE(std::string(out) ==
                "{\"unit\":\"merlinccu2\",\"label\":\"Merlin CCU (Dev)\",\"firmware\":\"cc3953d\","
                "\"board\":\"pico2_w\",\"uptime_s\":3725,\"wifi_connected\":true,\"rssi\":-61,"
                "\"ha_connected\":true,\"mqtt_connected\":false,\"free_heap\":12288,"
                "\"current_page\":\"HOME\",\"alerts_active\":2,\"top_alert\":\"HEAP LOW\","
                "\"units_online\":1,\"units_known\":2}\n");
}

HOST_TEST(status_json_escapes_text_and_gives_null_for_an_unknown_signal)
{
    status_json::Status status = sample();
    status.label = "Say \"hi\"\\\n";
    status.rssi_dbm.reset();
    status.top_alert = nullptr;
    char out[status_json::kMaxBytes] = {};
    EXPECT_TRUE(status_json::build(status, out, sizeof(out)));
    EXPECT_TRUE(std::strstr(out, "\"label\":\"Say \\\"hi\\\"\\\\\\u000a\"") != nullptr);
    EXPECT_TRUE(std::strstr(out, "\"rssi\":null") != nullptr);
    EXPECT_TRUE(std::strstr(out, "\"top_alert\":\"\"") != nullptr);
}

HOST_TEST(status_json_gives_an_empty_object_when_the_buffer_is_too_small)
{
    char out[40] = {};
    EXPECT_FALSE(status_json::build(sample(), out, sizeof(out)));
    EXPECT_TRUE(std::string(out) == "{}\n");
}
