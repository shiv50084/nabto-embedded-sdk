#ifndef _NM_POSIX_UDP_H_
#define _NM_POSIX_UDP_H_

#include <platform/np_udp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int nm_posix_socket;

struct nm_posix_received_ctx {
    np_udp_packet_received_callback cb;
    void* data;
    struct np_event event;
};

struct nm_posix_udp_socket {
    struct np_platform* pl;
    nm_posix_socket sock;
    struct nm_posix_received_ctx recv;
    enum np_ip_address_type type;
    np_communication_buffer* recvBuffer;
};

np_error_code nm_posix_udp_send_to(struct nm_posix_udp_socket* s, const struct np_udp_endpoint* ep, const uint8_t* buffer, uint16_t bufferSize);

void nm_posix_udp_event_try_recv_from(void* userData);

np_error_code nm_posix_bind_port(struct nm_posix_udp_socket* s, uint16_t port);
uint16_t nm_posix_udp_get_local_port(struct nm_posix_udp_socket* s);

np_error_code nm_posix_udp_create_socket_any(struct nm_posix_udp_socket* s);
np_error_code nm_posix_udp_create_socket_ipv6(struct nm_posix_udp_socket* s);
np_error_code nm_posix_udp_create_socket_ipv4(struct nm_posix_udp_socket* s);

#ifdef __cplusplus
} //extern "C"
#endif

#endif