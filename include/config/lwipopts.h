#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void merlinccu_set_ntp_time(uint32_t sec);
#ifdef __cplusplus
}
#endif

/// @brief lwIP configuration kept close to the official Pico W examples.
/// @details This is based on the Pico examples' common Wi-Fi settings, with no
/// socket API enabled and only the pieces needed for station-mode DHCP.

#define NO_SYS 1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

#define MEM_LIBC_MALLOC 0
#define MEM_ALIGNMENT 4
// Measured on a Pimoroni unit (/api/firmware lwip_mem_peak, 2026-11-27):
// 19.8 KB at peak with HA, MQTT (its 4 KB ring and panel pictures) and the
// web server sending its largest pages. 40 KB keeps twice that; it was 64.
#define MEM_SIZE (40 * 1024)

#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_DHCP 1
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_DNS 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_ALTCP 1
#define LWIP_ALTCP_TLS 1
#define LWIP_ALTCP_TLS_MBEDTLS 1
#define LWIP_SNTP 1

#define TCP_MSS 1460
#define TCP_WND (8 * TCP_MSS)
#define TCP_SND_BUF (8 * TCP_MSS)
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define MEMP_NUM_TCP_SEG 32
#define MEMP_NUM_SYS_TIMEOUT (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 10)
#define MEMP_NUM_ARP_QUEUE 10
// lwIP's MQTT client queues a whole publish in this ring before sending it,
// so it bounds the largest message. 4 KB carries the panel picture (#159,
// up to mqtt_manager's kScreenImageMaxBytes) plus its topic. It lives inside
// the MQTT client, allocated from MEM_SIZE.
#define MQTT_OUTPUT_RINGBUF_SIZE 4096
#define MQTT_REQ_TIMEOUT 10
#define MQTT_CONNECT_TIMOUT 10
// Received packets. In normal use one is held at a time (pbuf_pool_peak 1
// of 24). An over-the-air upload holds up to a receive window (TCP_WND, 8
// segments) unacknowledged while flash catches up, so 16 is that twice over.
#define PBUF_POOL_SIZE 16

#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_TX_SINGLE_PBUF 1

#define DHCP_DOES_ARP_CHECK 0
#define LWIP_DHCP_DOES_ACD_CHECK 0

#define LWIP_TIMEVAL_PRIVATE 0

#define SNTP_SERVER_DNS 1
#define SNTP_STARTUP_DELAY 0
#define SNTP_SET_SYSTEM_TIME(sec) merlinccu_set_ntp_time(sec)

// Issue #104: MEM_STATS/MEMP_STATS cost a small, fixed amount of static RAM
// (a handful of bytes per pool) in exchange for real high-water-mark data on
// lwIP's internal MEM_SIZE pool and PBUF_POOL_SIZE -- together the two
// biggest static RAM consumers after the display raster buffers, and the two
// with no existing usage data to size against. See debug_logging.h's
// kEnablePeriodicSerialOutput for how to read them.
#define MEM_STATS 1
#define SYS_STATS 0
#define MEMP_STATS 1
#define LINK_STATS 0

// Required by the CYW43 glue used by the Pico SDK.
#define CYW43_LWIP 1
