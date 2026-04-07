#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
void merlinccu_set_ntp_time(uint32_t sec);
#ifdef __cplusplus
}
#endif

/// @brief lwIP configuration kept close to the official Pico W examples.
/// @details This is based on the Pico examples' common Wi-Fi settings, with no
/// socket API enabled and only the pieces needed for station-mode DHCP.

#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0

#define MEM_LIBC_MALLOC                 0
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        4000

#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_DHCP                       1
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_DNS                        1
#define LWIP_TCP_KEEPALIVE              1
#define LWIP_SNTP                       1

#define TCP_MSS                         1460
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_SYS_TIMEOUT            (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 10)
#define MEMP_NUM_ARP_QUEUE              10
#define MQTT_OUTPUT_RINGBUF_SIZE        1024
#define MQTT_REQ_TIMEOUT                10
#define MQTT_CONNECT_TIMOUT             10
#define PBUF_POOL_SIZE                  24

#define LWIP_NETIF_HOSTNAME             1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_TX_SINGLE_PBUF       1

#define DHCP_DOES_ARP_CHECK             0
#define LWIP_DHCP_DOES_ACD_CHECK        0

#define LWIP_TIMEVAL_PRIVATE            0

#define SNTP_SERVER_DNS                 1
#define SNTP_STARTUP_DELAY              0
#define SNTP_SET_SYSTEM_TIME(sec)       merlinccu_set_ntp_time(sec)

#define MEM_STATS                       0
#define SYS_STATS                       0
#define MEMP_STATS                      0
#define LINK_STATS                      0

// Required by the CYW43 glue used by the Pico SDK.
#define CYW43_LWIP                      1
