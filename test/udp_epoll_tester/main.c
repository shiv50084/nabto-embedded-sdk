#include <platform/np_platform.h>
#include <platform/np_logging.h>
#include <modules/udp/epoll/nm_epoll.h>
#include <modules/communication_buffer/nm_unix_communication_buffer.h>
#include <modules/logging/nm_unix_logging.h>
#include <modules/timestamp/nm_unix_timestamp.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>

struct test_context {
    np_udp_socket* sock;
    int data;
};
struct np_platform pl;
uint8_t buffer[] = "Hello world";
uint16_t bufferSize = 12;
struct np_udp_endpoint ep;
struct np_timed_event ev;

void sent_callback(const np_error_code ec, void* data)
{
    struct test_context* ctx = (struct test_context*) data;
    NABTO_LOG_INFO(0, "sent, error code was: %i, and data: %i", ec, ctx->data);
}

void packet_sender(const np_error_code ec, void* data)
{
    struct test_context* ctx = (struct test_context*) data;
    pl.udp.async_send_to(ctx->sock, &ep, buffer, bufferSize, &sent_callback, data);
    np_event_queue_post_timed_event(&pl, &ev, 2000, &packet_sender, data);
}

void recv_callback(const np_error_code ec, struct np_udp_endpoint ep, np_communication_buffer* buffer, uint16_t bufferSize, void* data)
{
    struct test_context* ctx = (struct test_context*) data;
    NABTO_LOG_INFO(0, "Received: %s, with error code: %i", pl.buf.start(buffer), ec);
    pl.udp.async_recv_from(ctx->sock, &recv_callback, data);
}

void created(const np_error_code ec, np_udp_socket* socket, void* data){
    struct test_context* ctx = (struct test_context*) data;
    NABTO_LOG_INFO(0, "Created, error code was: %i, and data: %i", ec, ctx->data);
    ctx->sock = socket;
    packet_sender(NABTO_EC_OK, ctx);
    pl.udp.async_recv_from(socket, &recv_callback, data);
}

void destroyed(const np_error_code ec, void* data) {
    struct test_context* ctx = (struct test_context*) data;
    ctx->sock = NULL;
    NABTO_LOG_INFO(0, "Destroyed, error code was: %i, and data: %i", ec, ctx->data);
}

int main() {
    ep.port = 12345;
    inet_pton(AF_INET6, "::1", ep.ip.v6.addr);
    NABTO_LOG_INFO(0, "pl: %i", &pl);
    np_platform_init(&pl);
    nm_unix_comm_buf_init(&pl);
    nm_epoll_init(&pl);
    nm_unix_ts_init(&pl);
 
    np_log.log = &nm_unix_log;
    struct test_context data;
    data.data = 42;
    pl.udp.async_create(created, &data);

    while (true) {
        np_event_queue_execute_all(&pl);
        nm_epoll_wait();
    }

//    pl.udp.async_destroy(data.sock, destroyed, &data);
//    np_event_queue_poll_one(&pl);

    exit(0);
}
