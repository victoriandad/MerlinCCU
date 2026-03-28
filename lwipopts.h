#pragma once

/// @brief Minimal lwIP configuration for Pico W station-mode bring-up.
/// @details This project currently only needs DHCP-based IPv4 connectivity and
/// basic link status reporting. Higher-level protocols can be enabled later as
/// Home Assistant integration takes shape.

#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0
#define MEM_LIBC_MALLOC                 0
#define MEMP_MEM_MALLOC                 0
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
#define TCP_WND                         (8 * TCP_MSS)
#define TCP_MSS                         1460
#define TCP_SND_BUF                     (8 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((4 * TCP_SND_BUF + (TCP_MSS - 1)) / TCP_MSS)
#define MEMP_NUM_TCP_SEG                TCP_SND_QUEUELEN
#define PBUF_POOL_SIZE                  24
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_TIMEVAL_PRIVATE            0

// Required by the CYW43 driver glue used by the Pico SDK.
#define CYW43_LWIP                      1
