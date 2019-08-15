#include "nm_dtls_srv.h"
#include "nm_dtls_util.h"

#include <platform/np_logging.h>
#include <core/nc_version.h>

#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/certs.h>
#include <mbedtls/x509.h>
#include <mbedtls/ssl.h>
//#include <mbedtls/ssl_cookie.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>
#include <mbedtls/timing.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define NABTO_SSL_RECV_BUFFER_SIZE 4096

#define LOG NABTO_LOG_MODULE_DTLS_SRV
#define DEBUG_LEVEL 0

const char* nm_dtls_srv_alpnList[] = {NABTO_PROTOCOL_VERSION , NULL};

struct np_dtls_srv_connection {
    struct np_platform* pl;
    struct nc_client_connect* conn;
    struct nm_dtls_util_connection_ctx ctx;
    struct np_dtls_srv_send_context kaSendCtx;

    struct np_dtls_srv_send_context sendSentinel;
    struct np_event startSendEvent;


    np_dtls_srv_sender sender;
    np_dtls_srv_data_handler dataHandler;
    np_dtls_srv_event_handler eventHandler;
    void* senderData;
    bool sending;
    bool activeChannel;
};

struct np_dtls_srv {
    struct np_platform* pl;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt publicKey;
    mbedtls_pk_context privateKey;
};

// insert chunk into double linked list after elm.
void nm_dtls_srv_insert_send_data(struct np_dtls_srv_send_context* chunk, struct np_dtls_srv_send_context* elm)
{
    struct np_dtls_srv_send_context* before = elm;
    struct np_dtls_srv_send_context* after = elm->next;

    before->next = chunk;
    chunk->next = after;
    after->prev = chunk;
    chunk->prev = before;
}

void nm_dtls_srv_remove_send_data(struct np_dtls_srv_send_context* elm)
{
    struct np_dtls_srv_send_context* before = elm->prev;
    struct np_dtls_srv_send_context* after = elm->next;
    before->next = after;
    after->prev = before;
}

static np_error_code nm_dtls_srv_create(struct np_platform* pl, struct np_dtls_srv** server);
static void nm_dtls_srv_destroy(struct np_dtls_srv* server);


static np_error_code nm_dtls_srv_init_config(struct np_dtls_srv* server,
                                             const unsigned char* publicKeyL, size_t publicKeySize,
                                             const unsigned char* privateKeyL, size_t privateKeySize);

static np_error_code nm_dtls_srv_set_keys(struct np_dtls_srv* server,
                                          const unsigned char* publicKeyL, size_t publicKeySize,
                                          const unsigned char* privateKeyL, size_t privateKeySize);

static np_error_code nm_dtls_srv_create_connection(struct np_dtls_srv* server, struct np_dtls_srv_connection** dtls,
                                                   np_dtls_srv_sender sender,
                                                   np_dtls_srv_data_handler dataHandler,
                                                   np_dtls_srv_event_handler eventHandler, void* data);
static void nm_dtls_srv_destroy_connection(struct np_dtls_srv_connection* connection);

static np_error_code nm_dtls_srv_async_send_data(struct np_platform* pl, struct np_dtls_srv_connection* ctx,
                                                 struct np_dtls_srv_send_context* sendCtx);

static np_error_code nm_dtls_srv_async_close(struct np_platform* pl, struct np_dtls_srv_connection* ctx,
                                             np_dtls_close_callback cb, void* data);

static np_error_code nm_dtls_srv_get_fingerprint(struct np_platform* pl, struct np_dtls_srv_connection* ctx,
                                                 uint8_t* fp);


//static void nm_dtls_srv_tls_logger( void *ctx, int level, const char *file, int line, const char *str );
void nm_dtls_srv_connection_send_callback(const np_error_code ec, void* data);
void nm_dtls_srv_do_one(void* data);
void nm_dtls_srv_start_send(struct np_dtls_srv_connection* ctx);
void nm_dtls_srv_start_send_deferred(void* data);
void nm_dtls_srv_close_from_self(struct np_dtls_srv_connection* ctx);

// Function called by mbedtls when data should be sent to the network
int nm_dtls_srv_mbedtls_send(void* ctx, const unsigned char* buffer, size_t bufferSize);
// Function called by mbedtls when it wants data from the network
int nm_dtls_srv_mbedtls_recv(void* ctx, unsigned char* buffer, size_t bufferSize);
// Function called by mbedtls which creates timeout events
void nm_dtls_srv_mbedtls_timing_set_delay(void* ctx, uint32_t intermediateMilliseconds, uint32_t finalMilliseconds);
// Function called by mbedtls to determine when the next timeout event occurs
int nm_dtls_srv_mbedtls_timing_get_delay(void* ctx);

void nm_dtls_srv_event_send_to(void* data);


// Get the packet counters for given dtls_cli_context
np_error_code nm_dtls_srv_get_packet_count(struct np_dtls_srv_connection* ctx, uint32_t* recvCount, uint32_t* sentCount)
{
    *recvCount = ctx->ctx.recvCount;
    *sentCount = ctx->ctx.sentCount;
    return NABTO_EC_OK;
}

// Get the result of the application layer protocol negotiation
const char*  nm_dtls_srv_get_alpn_protocol(struct np_dtls_srv_connection* ctx) {
    return mbedtls_ssl_get_alpn_protocol(&ctx->ctx.ssl);
}

np_error_code nm_dtls_srv_handle_packet(struct np_platform* pl, struct np_dtls_srv_connection*ctx,
                                        uint8_t channelId, np_communication_buffer* buffer, uint16_t bufferSize);

np_error_code nm_dtls_srv_init(struct np_platform* pl)
{
    pl->dtlsS.create = &nm_dtls_srv_create;
    pl->dtlsS.destroy = &nm_dtls_srv_destroy;
    pl->dtlsS.set_keys = &nm_dtls_srv_set_keys;
    pl->dtlsS.create_connection = &nm_dtls_srv_create_connection;
    pl->dtlsS.destroy_connection = &nm_dtls_srv_destroy_connection;
    pl->dtlsS.destroy = &nm_dtls_srv_destroy;
    pl->dtlsS.async_send_data = &nm_dtls_srv_async_send_data;
    pl->dtlsS.async_close = &nm_dtls_srv_async_close;
    pl->dtlsS.get_fingerprint = &nm_dtls_srv_get_fingerprint;
    pl->dtlsS.get_alpn_protocol = &nm_dtls_srv_get_alpn_protocol;
    pl->dtlsS.get_packet_count = &nm_dtls_srv_get_packet_count;
    pl->dtlsS.handle_packet = &nm_dtls_srv_handle_packet;
    return NABTO_EC_OK;
}

/**
 * get peers fingerprint for given DTLS client context
 */
np_error_code nm_dtls_srv_get_fingerprint(struct np_platform* pl, struct np_dtls_srv_connection* ctx, uint8_t* fp)
{
    const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(&ctx->ctx.ssl);
    if (crt == NULL) {
        NABTO_LOG_ERROR(LOG, "Failed to get peer cert from mbedtls");
        NABTO_LOG_ERROR(LOG, "Verification returned %u", mbedtls_ssl_get_verify_result(&ctx->ctx.ssl));
        return NABTO_EC_FAILED;
    }
    return nm_dtls_util_fp_from_crt(crt, fp);
}

np_error_code nm_dtls_srv_create(struct np_platform* pl, struct np_dtls_srv** server)
{
    *server = calloc(1, sizeof(struct np_dtls_srv));
    (*server)->pl = pl;
    mbedtls_ssl_config_init( &(*server)->conf );
    mbedtls_entropy_init( &(*server)->entropy );
    mbedtls_ctr_drbg_init( &(*server)->ctr_drbg );
    mbedtls_x509_crt_init( &(*server)->publicKey );
    mbedtls_pk_init( &(*server)->privateKey );
    return NABTO_EC_OK;
}

void nm_dtls_srv_destroy(struct np_dtls_srv* server)
{
    mbedtls_ssl_config_free( &server->conf );
    mbedtls_entropy_free( &server->entropy );
    mbedtls_ctr_drbg_free( &server->ctr_drbg );
    mbedtls_x509_crt_free( &server->publicKey );
    mbedtls_pk_free( &server->privateKey );

    free(server);
}

np_error_code nm_dtls_srv_set_keys(struct np_dtls_srv* server,
                                   const unsigned char* publicKeyL, size_t publicKeySize,
                                   const unsigned char* privateKeyL, size_t privateKeySize)
{
    return nm_dtls_srv_init_config(server, publicKeyL, publicKeySize, privateKeyL, privateKeySize);
}

np_error_code nm_dtls_srv_create_connection(struct np_dtls_srv* server,
                                            struct np_dtls_srv_connection** dtls,
                                            np_dtls_srv_sender sender,
                                            np_dtls_srv_data_handler dataHandler,
                                            np_dtls_srv_event_handler eventHandler, void* data)
{
    int ret;
    *dtls = (struct np_dtls_srv_connection*)calloc(1, sizeof(struct np_dtls_srv_connection));
    if(!dtls) {
        return NABTO_EC_FAILED;
    }
    (*dtls)->pl = server->pl;
    (*dtls)->sender = sender;
    (*dtls)->dataHandler = dataHandler;
    (*dtls)->eventHandler = eventHandler;
    (*dtls)->senderData = data;
    (*dtls)->ctx.sslRecvBuf = server->pl->buf.allocate();
    (*dtls)->ctx.sslSendBuffer = server->pl->buf.allocate();
    (*dtls)->activeChannel = true;
    (*dtls)->sending = false;

    (*dtls)->sendSentinel.next = &(*dtls)->sendSentinel;
    (*dtls)->sendSentinel.prev = &(*dtls)->sendSentinel;

    NABTO_LOG_TRACE(LOG, "DTLS was allocated at: %u");
    //mbedtls connection initialization
    mbedtls_ssl_init( &((*dtls)->ctx.ssl) );
    if( ( ret = mbedtls_ssl_setup( &((*dtls)->ctx.ssl), &server->conf ) ) != 0 )
    {
        NABTO_LOG_ERROR(LOG, " failed ! mbedtls_ssl_setup returned %d", ret );
        return NABTO_EC_FAILED;
    }

    mbedtls_ssl_set_timer_cb( &((*dtls)->ctx.ssl), (*dtls), &nm_dtls_srv_mbedtls_timing_set_delay,
                              &nm_dtls_srv_mbedtls_timing_get_delay );

    mbedtls_ssl_session_reset( &((*dtls)->ctx.ssl) );

//    ret = mbedtls_ssl_set_client_transport_id(&((*dtls)->ssl), (const unsigned char*)conn, sizeof(np_connection));
//    if (ret != 0) {
//        NABTO_LOG_ERROR(LOG, "mbedtls_ssl_set_client_transport_id() returned -0x%x\n\n", -ret);
//        return NABTO_EC_FAILED;
//    }

    mbedtls_ssl_set_hs_authmode( &((*dtls)->ctx.ssl), MBEDTLS_SSL_VERIFY_OPTIONAL );

    ret = mbedtls_ssl_set_hs_own_cert(&((*dtls)->ctx.ssl), &server->publicKey, &server->privateKey);
    if (ret != 0) {
        NABTO_LOG_ERROR(LOG, "failed ! mbedtls_ssl_set_hs_own_cert returned %d", ret);
        return NABTO_EC_FAILED;
    }

    mbedtls_ssl_set_bio( &((*dtls)->ctx.ssl), (*dtls),
                         &nm_dtls_srv_mbedtls_send, &nm_dtls_srv_mbedtls_recv, NULL );

    return NABTO_EC_OK;
}

static void nm_dtls_srv_destroy_connection(struct np_dtls_srv_connection* connection)
{
    struct np_platform* pl = connection->pl;
    struct np_dtls_srv_connection* ctx = connection;
    ctx->ctx.state = CLOSING;
    // remove the first element until the list is empty
    while(ctx->sendSentinel.next != &ctx->sendSentinel) {
        struct np_dtls_srv_send_context* first = ctx->sendSentinel.next;
        nm_dtls_srv_remove_send_data(first);
        first->cb(NABTO_EC_CONNECTION_CLOSING, first->data);
    }
    np_event_queue_cancel_timed_event(ctx->pl, &ctx->ctx.tEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->ctx.closeEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->startSendEvent);
    pl->buf.free(connection->ctx.sslRecvBuf);
    pl->buf.free(connection->ctx.sslSendBuffer);
    mbedtls_ssl_free(&connection->ctx.ssl);
    free(connection);
}

np_error_code nm_dtls_srv_handle_packet(struct np_platform* pl, struct np_dtls_srv_connection*ctx,
                                        uint8_t channelId, np_communication_buffer* buffer, uint16_t bufferSize)
{
    NABTO_LOG_TRACE(LOG, "Handle packet called");
    // TODO: remove channel IDs from dtls srv
    ctx->ctx.currentChannelId = channelId;
    memcpy(ctx->ctx.recvBuffer, ctx->pl->buf.start(buffer), bufferSize);
    ctx->ctx.recvBufferSize = bufferSize;
    nm_dtls_srv_do_one(ctx);
    return NABTO_EC_OK;
}


void nm_dtls_srv_do_one(void* data)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*)data;
    if (ctx->ctx.state == CONNECTING) {
        int ret;
        ret = mbedtls_ssl_handshake( &ctx->ctx.ssl );
        if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
            ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            // keep state as CONNECTING
            NABTO_LOG_TRACE(LOG, "Keeping CONNECTING state");
        } else if (ret == 0) {
            NABTO_LOG_INFO(LOG, "State changed to DATA");

            ctx->ctx.state = DATA;
            ctx->eventHandler(NP_DTLS_SRV_EVENT_HANDSHAKE_COMPLETE, ctx->senderData);
        } else {
            NABTO_LOG_ERROR(LOG,  " failed  ! mbedtls_ssl_handshake returned -0x%04x", -ret );
            np_event_queue_cancel_timed_event(ctx->pl, &ctx->ctx.tEv);
            return;
        }
    } else if (ctx->ctx.state == DATA) {
        int ret;
        ret = mbedtls_ssl_read(&ctx->ctx.ssl, ctx->pl->buf.start(ctx->ctx.sslRecvBuf), ctx->pl->buf.size(ctx->ctx.sslRecvBuf) );
        if (ret == 0) {
            // EOF
            ctx->ctx.state = CLOSING;
            NABTO_LOG_TRACE(LOG, "Received EOF, state = CLOSING");
        } else if (ret > 0) {
            uint64_t seq = *((uint64_t*)ctx->ctx.ssl.in_ctr);
            ctx->ctx.recvCount++;
            ctx->dataHandler(ctx->ctx.currentChannelId, seq,
                             ctx->ctx.sslRecvBuf, ret, ctx->senderData);
            return;
        } else if (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                   ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            // OK
        } else {
            NABTO_LOG_ERROR(LOG, "Received ERROR: %i", ret);
            nm_dtls_srv_close_from_self(ctx);
        }
    }
}

void nm_dtls_srv_close_from_self(struct np_dtls_srv_connection* ctx)
{
    ctx->ctx.state = CLOSING;
    // remove the first element until the list is empty
    while(ctx->sendSentinel.next != &ctx->sendSentinel) {
        struct np_dtls_srv_send_context* first = ctx->sendSentinel.next;
        nm_dtls_srv_remove_send_data(first);
        first->cb(NABTO_EC_CONNECTION_CLOSING, first->data);
    }
    np_event_queue_cancel_timed_event(ctx->pl, &ctx->ctx.tEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->ctx.closeEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->startSendEvent);

    ctx->eventHandler(NP_DTLS_SRV_EVENT_CLOSED, ctx->senderData);
}

void nm_dtls_srv_start_send(struct np_dtls_srv_connection* ctx)
{
    np_event_queue_post(ctx->pl, &ctx->startSendEvent, &nm_dtls_srv_start_send_deferred, ctx);
}

void nm_dtls_srv_start_send_deferred(void* data)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (ctx->sending) {
        return;
    }

    if (ctx->sendSentinel.next == &ctx->sendSentinel) {
        // empty send queue
        return;
    }

    struct np_dtls_srv_send_context* next = ctx->sendSentinel.next;
    nm_dtls_srv_remove_send_data(next);

    int ret = mbedtls_ssl_write( &ctx->ctx.ssl, (unsigned char *) next->buffer, next->bufferSize );
    if (next->cb == NULL) {
        ctx->ctx.sentCount++;
    } else if (ret == MBEDTLS_ERR_SSL_BAD_INPUT_DATA) {
        // packet too large
        NABTO_LOG_ERROR(LOG, "ssl_write failed with: %i (Packet too large)", ret);
        next->cb(NABTO_EC_MALFORMED_PACKET, next->data);
    } else if (ret < 0) {
        // unknown error
        NABTO_LOG_ERROR(LOG, "ssl_write failed with: %i", ret);
        next->cb(NABTO_EC_FAILED, next->data);
    } else {
        ctx->ctx.sentCount++;
        next->cb(NABTO_EC_OK, next->data);
    }

    // can we send more packets?
    nm_dtls_srv_start_send(ctx);
}

np_error_code nm_dtls_srv_async_send_data(struct np_platform* pl, struct np_dtls_srv_connection* ctx,
                                          struct np_dtls_srv_send_context* sendCtx)
{
    if (ctx->ctx.state == CLOSING) {
        return NABTO_EC_CONNECTION_CLOSING;
    }
    NABTO_LOG_TRACE(LOG, "enqueued dtls application data packet");
    nm_dtls_srv_insert_send_data(sendCtx, &ctx->sendSentinel);
    nm_dtls_srv_start_send(ctx);
    return NABTO_EC_OK;
}

void nm_dtls_srv_event_close(void* data){
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (ctx->sending) {
        np_event_queue_post(ctx->pl, &ctx->ctx.closeEv, &nm_dtls_srv_event_close, ctx);
        return;
    }
    np_event_queue_cancel_timed_event(ctx->pl, &ctx->ctx.tEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->ctx.closeEv);
    np_event_queue_cancel_event(ctx->pl, &ctx->startSendEvent);

    np_dtls_close_callback cb = ctx->ctx.closeCb;
    void* cbData = ctx->ctx.closeCbData;
    ctx->ctx.closeCb = NULL;
    if(cb != NULL) {
        cb(NABTO_EC_OK, cbData);
    }
}

np_error_code nm_dtls_srv_async_close(struct np_platform* pl, struct np_dtls_srv_connection* ctx,
                                      np_dtls_close_callback cb, void* data)
{
    if (!ctx || ctx->ctx.state == CLOSING) {
        return NABTO_EC_OK;
    }
    ctx->ctx.closeCb = cb;
    ctx->ctx.closeCbData = data;
    ctx->ctx.state = CLOSING;
    mbedtls_ssl_close_notify(&ctx->ctx.ssl);
    np_event_queue_post(ctx->pl, &ctx->ctx.closeEv, &nm_dtls_srv_event_close, ctx);
    return NABTO_EC_OK;
}

#if defined(MBEDTLS_DEBUG_C)
static void nm_dtls_srv_tls_logger( void *ctx, int level,
                                    const char *file, int line,
                                    const char *str )
{
    ((void) level);
    uint32_t severity;
    switch (level) {
        case 1:
            severity = NABTO_LOG_SEVERITY_ERROR;
            break;
        case 2:
            severity = NABTO_LOG_SEVERITY_INFO;
            break;
        default:
            severity = NABTO_LOG_SEVERITY_TRACE;
            break;
    }
    // TODO: fix this ugly hack to remove \n after all mbedtls log strings
    NABTO_LOG_RAW(severity, LOG, line, file, str );
}
#endif


np_error_code nm_dtls_srv_init_config(struct np_dtls_srv* server,
                                      const unsigned char* publicKeyL, size_t publicKeySize,
                                      const unsigned char* privateKeyL, size_t privateKeySize)
{
    const char *pers = "dtls_server";
    int ret;
#if defined(MBEDTLS_DEBUG_C)
    mbedtls_debug_set_threshold( DEBUG_LEVEL );
#endif

    if( ( ret = mbedtls_ssl_config_defaults( &server->conf,
                                             MBEDTLS_SSL_IS_SERVER,
                                             MBEDTLS_SSL_TRANSPORT_DATAGRAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
    {
        NABTO_LOG_ERROR(LOG, " failed ! mbedtls_ssl_config_defaults returned %i", ret);
        return NABTO_EC_FAILED;
    }

    mbedtls_ssl_conf_alpn_protocols(&server->conf, nm_dtls_srv_alpnList );

    if( ( ret = mbedtls_ctr_drbg_seed( &server->ctr_drbg, mbedtls_entropy_func, &server->entropy,
                                       (const unsigned char *) pers,
                                       strlen( pers ) ) ) != 0 )
    {
        NABTO_LOG_ERROR(LOG, " failed ! mbedtls_ctr_drbg_seed returned %d", ret );
        return NABTO_EC_FAILED;
    }
    mbedtls_ssl_conf_rng( &server->conf, mbedtls_ctr_drbg_random, &server->ctr_drbg );

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_ssl_conf_dbg( &server->conf, &nm_dtls_srv_tls_logger, stdout );
#endif

    ret = mbedtls_x509_crt_parse( &server->publicKey, (const unsigned char*)publicKeyL, publicKeySize+1);
    if( ret != 0 )
    {
        NABTO_LOG_ERROR(LOG, "mbedtls_x509_crt_parse returned %d ", ret);
        return NABTO_EC_FAILED;
    }

    ret =  mbedtls_pk_parse_key( &server->privateKey, (const unsigned char*)privateKeyL, privateKeySize+1, NULL, 0 );
    if( ret != 0 )
    {
        NABTO_LOG_ERROR(LOG,"mbedtls_pk_parse_key returned %d", ret);
        return NABTO_EC_FAILED;
    }

    if( ( ret = mbedtls_ssl_conf_own_cert( &server->conf, &server->publicKey, &server->privateKey ) ) != 0 )
    {
        NABTO_LOG_ERROR(LOG,"mbedtls_ssl_conf_own_cert returned %d", ret);
        return NABTO_EC_FAILED;
    }
    return NABTO_EC_OK;
}

// Function called by mbedtls when data should be sent to the network
int nm_dtls_srv_mbedtls_send(void* data, const unsigned char* buffer, size_t bufferSize)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (!ctx->sending) {
        memcpy(ctx->pl->buf.start(ctx->ctx.sslSendBuffer), buffer, bufferSize);
        NABTO_LOG_TRACE(LOG, "mbedtls wants write %u bytes:", bufferSize);
        NABTO_LOG_BUF(LOG, buffer, bufferSize);
        ctx->ctx.sslSendBufferSize = bufferSize;
        ctx->sending = true;
        ctx->activeChannel = true;
        ctx->sender(ctx->activeChannel, ctx->ctx.sslSendBuffer, bufferSize, &nm_dtls_srv_connection_send_callback, ctx, ctx->senderData);

        return bufferSize;
    } else {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

}

void nm_dtls_srv_connection_send_callback(const np_error_code ec, void* data)
{

    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (data == NULL) {
        return;
    }
    ctx->sending = false;
    if (ec != NABTO_EC_OK) {
        NABTO_LOG_ERROR(LOG, "Connection Async Send failed with code: %u", ec);
        return;
    }
    ctx->ctx.sslSendBufferSize = 0;
    if(ctx->ctx.state == CLOSING) {
        return;
    }
    nm_dtls_srv_do_one(ctx);
    nm_dtls_srv_start_send(ctx);
}


// Function called by mbedtls when it wants data from the network
int nm_dtls_srv_mbedtls_recv(void* data, unsigned char* buffer, size_t bufferSize)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (ctx->ctx.recvBufferSize == 0) {
        NABTO_LOG_TRACE(LOG, "Empty buffer, returning WANT_READ");
        return MBEDTLS_ERR_SSL_WANT_READ;
    } else {
        NABTO_LOG_TRACE(LOG, "mbtls wants read %u bytes into buffersize: %u", ctx->ctx.recvBufferSize, bufferSize);
        size_t maxCp = bufferSize > ctx->ctx.recvBufferSize ? ctx->ctx.recvBufferSize : bufferSize;
        memcpy(buffer, ctx->ctx.recvBuffer, maxCp);
        NABTO_LOG_TRACE(LOG, "returning %i bytes to mbedtls:", maxCp);
//        NABTO_LOG_BUF(LOG, buffer, maxCp);
        ctx->ctx.recvBufferSize = 0;
        return maxCp;
    }
}

void nm_dtls_srv_timed_event_do_one(const np_error_code ec, void* data) {
    nm_dtls_srv_do_one(data);
}

// Function called by mbedtls which creates timeout events
void nm_dtls_srv_mbedtls_timing_set_delay(void* data, uint32_t intermediateMilliseconds, uint32_t finalMilliseconds)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (finalMilliseconds == 0) {
        // able current timer
        np_event_queue_cancel_timed_event(ctx->pl, &ctx->ctx.tEv);
        ctx->ctx.finalTp = 0;
    } else {
        ctx->pl->ts.set_future_timestamp(&ctx->ctx.intermediateTp, intermediateMilliseconds);
        ctx->pl->ts.set_future_timestamp(&ctx->ctx.finalTp, finalMilliseconds);
        np_event_queue_post_timed_event(ctx->pl, &ctx->ctx.tEv, finalMilliseconds, &nm_dtls_srv_timed_event_do_one, ctx);
    }
}

// Function called by mbedtls to determine when the next timeout event occurs
int nm_dtls_srv_mbedtls_timing_get_delay(void* data)
{
    struct np_dtls_srv_connection* ctx = (struct np_dtls_srv_connection*) data;
    if (ctx->ctx.finalTp) {
        if (ctx->pl->ts.passed_or_now(&ctx->ctx.finalTp)) {
            return 2;
        } else if (ctx->pl->ts.passed_or_now(&ctx->ctx.intermediateTp)) {
            return 1;
        } else {
            return 0;
        }
    } else {
        return -1;
    }
}
