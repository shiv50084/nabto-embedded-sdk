
#include "nc_attacher.h"

#include <core/nc_packet.h>
#include <platform/np_logging.h>
#include <core/nc_version.h>

#include <string.h>

#define LOG NABTO_LOG_MODULE_ATTACHER

//struct nc_attach_context ctx;

const char* attachPath[2] = {"device", "attach"};

void nc_attacher_dns_cb(const np_error_code ec, struct np_ip_address* rec, size_t recSize, void* data);
void nc_attacher_dtls_conn_cb(const np_error_code ec, np_dtls_cli_context* crypCtx, void* data);
void nc_attacher_dtls_closed_cb(const np_error_code ec, void* data);
void nc_attacher_coap_request_handler(struct nabto_coap_client_request* request, void* userData);
void nc_attacher_dtls_recv_cb(const np_error_code ec, uint8_t channelId, uint64_t sequence,
                                 np_communication_buffer* buf, uint16_t bufferSize, void* data);


void nc_attacher_init(struct nc_attach_context* ctx, struct np_platform* pl, struct nc_coap_client_context* coapClient)
{
    memset(ctx, 0, sizeof(struct nc_attach_context));
    ctx->pl = pl;
    ctx->dtls = pl->dtlsC.create(pl);
    ctx->coapClient = coapClient;
}
void nc_attacher_deinit(struct nc_attach_context* ctx)
{
    ctx->pl->dtlsC.destroy(ctx->dtls);
    // cleanup/close dtls connections etc.
}

np_error_code nc_attacher_set_keys(struct nc_attach_context* ctx, const unsigned char* publicKeyL, size_t publicKeySize, const unsigned char* privateKeyL, size_t privateKeySize)
{
    return ctx->pl->dtlsC.set_keys(ctx->dtls, publicKeyL, publicKeySize, privateKeyL, privateKeySize);
}

np_error_code nc_attacher_async_attach(struct nc_attach_context* ctx, struct np_platform* pl,
                                       const struct nc_attach_parameters* params,
                                       nc_attached_callback cb, void* data)
{
    ctx->pl = pl;
    ctx->cb = cb;
    ctx->cbData = data;
    ctx->params = params;
    ctx->udp = params->udp;
    ctx->state = NC_ATTACHER_RESOLVING_DNS;
    ctx->detaching = false;

    memcpy(ctx->dns, ctx->params->hostname, strlen(ctx->params->hostname)+1);
    ctx->pl->dns.async_resolve(ctx->pl, ctx->dns, &nc_attacher_dns_cb, ctx);
    return NABTO_EC_OK;
}


void nc_attacher_dns_cb(const np_error_code ec, struct np_ip_address* rec, size_t recSize, void* data)
{
    struct nc_attach_context* ctx = (struct nc_attach_context*)data;
    if (ec != NABTO_EC_OK) {
        NABTO_LOG_ERROR(LOG, "Failed to resolve attach dispatcher host");
        ctx->cb(ec, ctx->cbData);
        return;
    }
    if (recSize == 0 || ctx->detaching) {
        NABTO_LOG_ERROR(LOG, "Empty record list or detaching");
        ctx->cb(NABTO_EC_FAILED, ctx->cbData);
        return;
    }
    ctx->state = NC_ATTACHER_CONNECTING_TO_BS;
    // TODO: get load_balancer_port from somewhere
    ctx->ep.port = LOAD_BALANCER_PORT;
    // TODO: Pick a record which matches the supported protocol IPv4/IPv6 ?
    for (int i = 0; i < recSize; i++) {
    }
    memcpy(&ctx->ep.ip, &rec[0], sizeof(struct np_ip_address));
    ctx->pl->dtlsC.async_connect(ctx->pl, ctx->dtls, ctx->udp, ctx->ep, &nc_attacher_dtls_conn_cb, ctx);
}


void nc_attacher_dtls_conn_cb(const np_error_code ec, np_dtls_cli_context* crypCtx, void* data)
{
    struct nc_attach_context* ctx = (struct nc_attach_context*)data;
    uint8_t* ptr;
    uint8_t* start;
    uint8_t extBuffer[34];
    struct nabto_coap_client_request* req;
    if( ec != NABTO_EC_OK ) {
        ctx->cb(ec, ctx->cbData);
        return;
    }
    if( ctx->pl->dtlsC.get_alpn_protocol(crypCtx) == NULL ) {
        NABTO_LOG_ERROR(LOG, "Application Layer Protocol Negotiation failed for Basestation connection");
        ctx->pl->dtlsC.async_close(ctx->pl, crypCtx, &nc_attacher_dtls_closed_cb, ctx);
        ctx->cb(NABTO_EC_ALPN_FAILED, ctx->cbData);
        return;
    }
    ctx->state = NC_ATTACHER_CONNECTED_TO_BS;
    ctx->dtls = crypCtx;

    req = nabto_coap_client_request_new(nc_coap_client_get_client(ctx->coapClient),
                                        NABTO_COAP_METHOD_POST,
                                        2, attachPath,
                                        &nc_attacher_coap_request_handler,
                                        ctx, crypCtx);
    nabto_coap_client_request_set_content_format(req, NABTO_COAP_CONTENT_FORMAT_APPLICATION_N5);

    ctx->buffer = ctx->pl->buf.allocate();
    ptr = ctx->pl->buf.start(ctx->buffer);
    start = ptr;

    extBuffer[0] = (uint8_t)strlen(NABTO_VERSION);
    memcpy(&extBuffer[1], NABTO_VERSION, strlen(NABTO_VERSION));
    ptr = insert_packet_extension(ctx->pl, ptr, EX_NABTO_VERSION, extBuffer, strlen(NABTO_VERSION)+1);

    extBuffer[0] = strlen(ctx->params->appVersion);
    memcpy(&extBuffer[1], ctx->params->appVersion, strlen(ctx->params->appVersion));
    ptr = insert_packet_extension(ctx->pl, ptr, EX_APPLICATION_VERSION, extBuffer, strlen(ctx->params->appVersion));

    extBuffer[0] = strlen(ctx->params->appName);
    memcpy(&extBuffer[1], ctx->params->appName, strlen(ctx->params->appName));
    ptr = insert_packet_extension(ctx->pl, ptr, EX_APPLICATION_NAME, extBuffer, strlen(ctx->params->appName));

    NABTO_LOG_TRACE(LOG, "Sending attach CoAP Request:");
    NABTO_LOG_BUF(LOG, start, ptr - start);

    nabto_coap_client_request_set_payload(req, start, ptr - start);
    nabto_coap_client_request_send(req);

    ctx->pl->dtlsC.async_recv_from(ctx->pl, ctx->dtls, AT_DEVICE_RELAY, &nc_attacher_dtls_recv_cb, ctx);
}

void nc_attacher_dtls_closed_cb(const np_error_code ec, void* data)
{
    NABTO_LOG_INFO(LOG, "Dtls connection closed callback");
}

void nc_attacher_dtls_recv_cb(const np_error_code ec, uint8_t channelId, uint64_t sequence,
                              np_communication_buffer* buffer, uint16_t bufferSize, void* data)
{
    struct nc_attach_context* ctx = (struct nc_attach_context*)data;
    if (ec != NABTO_EC_OK) {
        if (ctx->detachCb) {
            nc_detached_callback cb = ctx->detachCb;
            ctx->detachCb = NULL;
            cb(ec, ctx->detachCbData);
        }
        return;
    }
    NABTO_LOG_TRACE(LOG, "recv cb from dtls, passing to coap");
    // TODO: if (!ctx->verified) { verify bs fingerprint }
    nc_coap_client_handle_packet(ctx->coapClient, buffer, bufferSize, ctx->dtls);
    ctx->pl->dtlsC.async_recv_from(ctx->pl, ctx->dtls, 42 /* unused */, &nc_attacher_dtls_recv_cb, ctx);
}

void nc_attacher_coap_request_handler(struct nabto_coap_client_request* request, void* data)
{
    NABTO_LOG_INFO(LOG, "Received CoAP response");
    uint8_t* ptr;
    const uint8_t* start;
    size_t bufferSize;

    struct nc_attach_context* ctx = (struct nc_attach_context*)data;
    struct nabto_coap_client_response* res = nabto_coap_client_request_get_response(request);
    if (!res) {
        // Request failed
        NABTO_LOG_ERROR(LOG, "Coap request failed, no response");
        ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, &nc_attacher_dtls_closed_cb, ctx);
        if (ctx->detachCb) {
            nc_detached_callback cb = ctx->detachCb;
            ctx->detachCb = NULL;
            cb(NABTO_EC_FAILED, ctx->detachCbData);
        }
        return;
    }
    uint8_t resCode = nabto_coap_client_response_get_code(res);
    if (resCode != NABTO_COAP_CODE_CREATED) {
        NABTO_LOG_ERROR(LOG, "BS returned CoAP error code: %d", resCode);
        ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, &nc_attacher_dtls_closed_cb, ctx);
        if (ctx->detachCb) {
            nc_detached_callback cb = ctx->detachCb;
            ctx->detachCb = NULL;
            cb(NABTO_EC_FAILED, ctx->detachCbData);
        }
        return;
    }
    if (!nabto_coap_client_response_get_payload(res, &start, &bufferSize)) {
        NABTO_LOG_ERROR(LOG, "No payload in CoAP response");
        ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, &nc_attacher_dtls_closed_cb, ctx);
        if (ctx->detachCb) {
            nc_detached_callback cb = ctx->detachCb;
            ctx->detachCb = NULL;
            cb(NABTO_EC_FAILED, ctx->detachCbData);
        }
        return;
    }
    ptr = (uint8_t*)start;
    while(ptr + 4 < start+bufferSize) { // while still space for an extension header
        if(uint16_read(ptr) == EX_KEEP_ALIVE_SETTINGS) {
            uint32_t interval;
            uint8_t retryInt, maxRetries;
            NABTO_LOG_TRACE(LOG,"Found EX_KEEP_ALIVE_SETTINGS");
            ptr += 4; // skip extension header
            interval = uint32_read(ptr);
            ptr += 4;
            retryInt = *ptr;
            ptr++;
            maxRetries = *ptr;
            NABTO_LOG_TRACE(LOG, "starting ka with int: %u, retryInt: %u, maxRetries: %u", interval, retryInt, maxRetries);
            ctx->pl->dtlsC.start_keep_alive(ctx->dtls, interval, retryInt, maxRetries);
        } else if (uint16_read(ptr) == EX_ATTACH_STATUS) {
            uint8_t status;
            ptr += 4; // skip extension header
            status = *ptr;
            ptr++;
            if (status == ATTACH_STATUS_ATTACHED) {
                // SUCCESS, just continue
            } else if (status == ATTACH_STATUS_REDIRECT) {
                uint8_t* dns = NULL;
                while (true) {
                    uint16_t extType = uint16_read(ptr);
                    uint16_t extLen = uint16_read(ptr+2);
                    if (extType == EX_DTLS_EP) {
                        ctx->ep.port = uint16_read(ptr+4);
                        // TODO: look at fingerprint as well
                        ctx->dnsLen = *(ptr+23); // skip header + port + az + fp = 4+2+1+16 = 23
                        dns = ptr+24;
                        NABTO_LOG_TRACE(LOG, "Found DNS extension with port: %u, dns: %s",
                                        ctx->ep.port, (char*)dns);
                        memcpy(ctx->dns, dns, ctx->dnsLen);
                        break;
                    }
                    ptr = ptr + extLen + 4;
                    if (ptr - start >= bufferSize) {
                        NABTO_LOG_ERROR(LOG, "Failed to find DNS extension in CT_DEVICE_LB_REDIRECT");
                        ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, &nc_attacher_dtls_closed_cb, ctx);
                        ctx->cb(NABTO_EC_MALFORMED_PACKET, ctx->cbData);
                        return;
                    }
                }
                ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, &nc_attacher_dtls_closed_cb, ctx);
                ctx->pl->dns.async_resolve(ctx->pl, ctx->dns, &nc_attacher_dns_cb, ctx);
                return;
            } else {
                // Should not happen, lets just assume it means attached
            }
            ctx->state = NC_ATTACHER_ATTACHED;
        } else {
            ptr += uint16_read(ptr+2) + 4;
        }
    }
    ctx->cb(NABTO_EC_OK, ctx->cbData);

}

np_error_code nc_attacher_register_detach_callback(struct nc_attach_context* ctx, nc_detached_callback cb, void* data)
{
    ctx->detachCb = cb;
    ctx->detachCbData = data;
    return NABTO_EC_OK;
}

np_error_code nc_attacher_detach(struct nc_attach_context* ctx)
{
    switch (ctx->state) {
        case NC_ATTACHER_RESOLVING_DNS:
            ctx->detaching = true;
            break;
        case NC_ATTACHER_CONNECTING_TO_BS:
        case NC_ATTACHER_CONNECTED_TO_BS:
        case NC_ATTACHER_ATTACHED:
            ctx->pl->dtlsC.async_close(ctx->pl, ctx->dtls, ctx->detachCb, ctx->detachCbData);
            break;
    }
    return NABTO_EC_OK;
}
