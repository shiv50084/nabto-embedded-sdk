#ifndef NC_CLIENT_CONNECT_H
#define NC_CLIENT_CONNECT_H

#include <platform/np_platform.h>

#define NC_CLIENT_CONNECT_MAX_CHANNELS 16

struct nc_connection_channel {
    enum np_channel_type type;
    struct np_udp_socket* sock;
    np_udp_endpoint ep;
    uint8_t channelId;
    bool active;
};

struct nc_connection_id {
    uint8_t id[16];
};

struct nc_client_connection {
    struct np_platform* pl;
    struct np_dtls_srv_connection* dtls;
    struct nc_connection_channel channels[NC_CLIENT_CONNECT_MAX_CHANNELS]; // several application channels can exist
    struct nc_connection_id id;
    uint8_t clientFingerprint[16];
    bool verified;
    struct nc_connection_channel* activeChannel;

    struct np_event ev;    
    np_dtls_srv_send_callback sentCb;
    void* sentData;
    np_error_code ec;
};


np_error_code nc_client_connect_open(struct np_platform* pl, struct nc_client_connection* conn,
                                     struct np_udp_socket* sock, struct np_udp_endpoint ep,
                                     np_communication_buffer* buffer, uint16_t bufferSize);

np_error_code nc_client_connect_handle_packet(struct np_platform* pl, struct nc_client_connection* conn,
                                              struct np_udp_socket* sock, struct np_udp_endpoint ep,
                                              np_communication_buffer* buffer, uint16_t bufferSize);

void nc_client_connect_close_connection(struct np_platform* pl, struct nc_client_connection* conn,
                                        np_error_code ec);

void nc_client_connect_dtls_recv_callback(const np_error_code ec, uint8_t channelId, uint64_t sequence,
                                          np_communication_buffer* buffer, uint16_t bufferSize, void* data);

void nc_client_connect_dtls_closed_cb(const np_error_code ec, void* data);

struct np_dtls_srv_connection* nc_client_connect_get_dtls_connection(struct nc_client_connection* conn);

void nc_client_connect_async_send_to_udp(uint8_t channelId,
                                         np_communication_buffer* buffer, uint16_t bufferSize,
                                         np_dtls_srv_send_callback cb, void* data);


#endif //_NC_CLIENT_CONNECT_H_
