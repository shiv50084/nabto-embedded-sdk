#include <nabto/nabto_device.h>
#include <nabto/nabto_device_experimental.h>
#include <api/nabto_device_defines.h>
#include <api/nabto_device_stream.h>
#include <api/nabto_device_coap.h>
#include <api/nabto_device_future.h>
#include <api/nabto_device_event_handler.h>
#include <api/nabto_api_future_queue.h>
#include <api/nabto_platform.h>
#include <api/nabto_device_coap.h>
#include <api/nabto_device_authorization.h>
#include <platform/np_error_code.h>

#include <platform/np_logging.h>
#include <platform/np_error_code.h>
#include <core/nc_version.h>
#include <core/nc_client_connection.h>

#include <modules/logging/api/nm_api_logging.h>
#include <modules/dtls/nm_dtls_util.h>

#include <stdlib.h>

#define LOG NABTO_LOG_MODULE_API

void* nabto_device_network_thread(void* data);
void* nabto_device_core_thread(void* data);
void nabto_device_init_platform(struct np_platform* pl);
void nabto_device_free_threads(struct nabto_device_context* dev);
NabtoDeviceError  nabto_device_create_crt_from_private_key(struct nabto_device_context* dev);
void nabto_device_do_stop(struct nabto_device_context* dev);

const char* NABTO_DEVICE_API nabto_device_version()
{
    return nc_version();
}

void notify_event_queue_post(void* data)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)data;
    nabto_device_threads_cond_signal(dev->eventCond);
}

void nabto_device_new_resolve_failure(struct nabto_device_context* dev)
{
    dev->closing = true;
    nabto_device_do_stop(dev);
    nabto_device_free((NabtoDevice*)dev);
}

/**
 * Allocate new device
 */
NabtoDevice* NABTO_DEVICE_API nabto_device_new()
{
    struct nabto_device_context* dev = (struct nabto_device_context*)malloc(sizeof(struct nabto_device_context));
    np_error_code ec;
    if (dev == NULL) {
        return NULL;
    }
    memset(dev, 0, sizeof(struct nabto_device_context));

    nabto_device_init_platform(&dev->pl);
    ec = nabto_device_init_platform_modules(&dev->pl);
    if (ec != NABTO_EC_OK) {
        NABTO_LOG_ERROR(LOG, "Failed to initialize platform modules");
        return NULL;
    }
    dev->closing = false;
    dev->eventMutex = nabto_device_threads_create_mutex();
    if (dev->eventMutex == NULL) {
        NABTO_LOG_ERROR(LOG, "mutex init has failed");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }
    dev->eventCond = nabto_device_threads_create_condition();
    if (dev->eventCond == NULL) {
        NABTO_LOG_ERROR(LOG, "condition init has failed");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }
    dev->futureQueueMutex = nabto_device_threads_create_mutex();
    if (dev->futureQueueMutex == NULL) {
        NABTO_LOG_ERROR(LOG, "future queue mutex init has failed");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }

    dev->coreThread = nabto_device_threads_create_thread();
    dev->networkThread = nabto_device_threads_create_thread();
    if (dev->coreThread == NULL || dev->networkThread == NULL) {
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }

    np_event_queue_init(&dev->pl, &notify_event_queue_post, dev);

    if (nabto_device_threads_run(dev->coreThread, nabto_device_core_thread, dev) != 0) {
        NABTO_LOG_ERROR(LOG, "Failed to run core thread");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }
    if (nabto_device_threads_run(dev->networkThread, nabto_device_network_thread, dev) != 0) {
        NABTO_LOG_ERROR(LOG, "Failed to run network thread");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }

    nabto_device_authorization_init_module(dev);

    ec = nc_device_init(&dev->core, &dev->pl);
    if (ec != NABTO_EC_OK) {
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }

    ec = nm_tcp_tunnels_init(&dev->tcpTunnels, &dev->core);
    if (ec != NABTO_EC_OK) {
        NABTO_LOG_ERROR(LOG, "Failed to start tcp tunnelling module");
        nabto_device_new_resolve_failure(dev);
        return NULL;
    }

    np_list_init(&dev->listeners);

    return (NabtoDevice*)dev;
}

/**
 * block until no further work is done.
 */
void NABTO_DEVICE_API nabto_device_stop(NabtoDevice* device)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;

    if (dev->closing) {
        return;
    }

    nabto_device_listener_stop_all(dev);

    nabto_device_threads_mutex_lock(dev->eventMutex);

    nm_tcp_tunnels_deinit(&dev->tcpTunnels);
    nc_device_deinit(&dev->core);

    dev->closing = true;
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    nabto_device_do_stop(dev);
}

void nabto_device_do_stop(struct nabto_device_context* dev)
{
    // Send a signal if a function is blocking the network thread.
    nabto_device_platform_signal(&dev->pl);

    if (dev->eventCond != NULL) {
        nabto_device_threads_cond_signal(dev->eventCond);
    }

    if (dev->networkThread != NULL) {
        nabto_device_threads_join(dev->networkThread);
    }
    if (dev->coreThread != NULL) {
        nabto_device_threads_join(dev->coreThread);
    }

    nabto_device_platform_close(&dev->pl);
}

/**
 * free device when closed
 */
void NABTO_DEVICE_API nabto_device_free(NabtoDevice* device)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;

    nabto_device_stop(device);
    nabto_device_free_threads(dev);

    free(dev->productId);
    free(dev->deviceId);
    free(dev->serverUrl);
    free(dev->publicKey);
    free(dev->privateKey);


    nabto_device_deinit_platform_modules(&dev->pl);
    nabto_device_deinit_platform(&dev->pl);

    free(dev);
}

/**
 * Self explanetory set functions
 */
NabtoDeviceError NABTO_DEVICE_API nabto_device_set_product_id(NabtoDevice* device, const char* str)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);

    free(dev->productId);

    dev->productId = strdup(str);
    if (dev->productId == NULL) {
        ec = NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);

    return ec;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_device_id(NabtoDevice* device, const char* str)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    free(dev->deviceId);

    dev->deviceId = strdup(str);
    if (dev->deviceId == NULL) {
        ec = NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_server_url(NabtoDevice* device, const char* str)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    free(dev->serverUrl);

    dev->serverUrl = strdup(str);
    if (dev->serverUrl == NULL) {
        ec = NABTO_DEVICE_EC_OUT_OF_MEMORY;
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_server_port(NabtoDevice* device, uint16_t port)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    dev->core.serverPort = port;
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_private_key(NabtoDevice* device, const char* str)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    free(dev->privateKey);

    dev->privateKey = strdup(str);
    if (dev->privateKey == NULL) {
        ec = NABTO_DEVICE_EC_OUT_OF_MEMORY;
    } else {
        char* crt;
        ec = nm_dtls_create_crt_from_private_key(dev->privateKey, &crt);
        if (dev->publicKey != NULL) {
            free(dev->publicKey);
            dev->publicKey = NULL;
        }
        dev->publicKey = crt;
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);

}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_app_name(NabtoDevice* device, const char* name)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    if (strlen(name) > 32) {
        return NABTO_DEVICE_EC_STRING_TOO_LONG;
    }
    nabto_device_threads_mutex_lock(dev->eventMutex);
    memcpy(dev->appName, name, strlen(name));
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_app_version(NabtoDevice* device, const char* version)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    if (strlen(version) > 32) {
        return NABTO_DEVICE_EC_STRING_TOO_LONG;
    }
    nabto_device_threads_mutex_lock(dev->eventMutex);
    memcpy(dev->appVersion, version, strlen(version));
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_local_port(NabtoDevice* device, uint16_t port)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    dev->port = port;
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_get_local_port(NabtoDevice* device, uint16_t* port)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    uint16_t p = 0;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    p = nc_udp_dispatch_get_local_port(&dev->core.udp);
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    if (p == 0) {
        return NABTO_DEVICE_EC_INVALID_STATE;
    } else {
        *port = p;
    }
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_enable_mdns(NabtoDevice* device)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    dev->enableMdns = true;
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}

/**
 * Starting the device
 */
NabtoDeviceError NABTO_DEVICE_API nabto_device_start(NabtoDevice* device)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec;
    if (dev->publicKey == NULL || dev->privateKey == NULL || dev->serverUrl == NULL) {
        NABTO_LOG_ERROR(LOG, "Encryption key pair or server URL not set");
        return NABTO_DEVICE_EC_INVALID_STATE;
    }
    if (dev->deviceId == NULL || dev->productId == NULL) {
        NABTO_LOG_ERROR(LOG, "Missing deviceId or productdId");
        return NABTO_DEVICE_EC_INVALID_STATE;
    }


    nabto_device_threads_mutex_lock(dev->eventMutex);
    // Init platform
    nc_device_set_keys(&dev->core, (const unsigned char*)dev->publicKey, strlen(dev->publicKey), (const unsigned char*)dev->privateKey, strlen(dev->privateKey));

    // start the core
    ec = nc_device_start(&dev->core, dev->appName, dev->appVersion, dev->productId, dev->deviceId, dev->serverUrl, dev->port, dev->enableMdns);

    if ( ec != NABTO_EC_OK ) {
        NABTO_LOG_ERROR(LOG, "Failed to start device core");
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);
}

static char* toHex(uint8_t* data, size_t dataLength)
{
    size_t outputLength = dataLength*2 + 1;
    char* output = (char*)malloc(outputLength);
    if (output == NULL) {
        return output;
    }
    memset(output,0,outputLength);
    size_t i;
    for (i = 0; i < dataLength; i++) {
        size_t outputOffset = i*2;
        sprintf(output+outputOffset, "%02x", data[i]);
    }
    return output;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_get_device_fingerprint_hex(NabtoDevice* device, char** fingerprint)
{
    *fingerprint = NULL;
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (dev->privateKey == NULL) {
        ec = NABTO_DEVICE_EC_INVALID_STATE;
    }
    uint8_t hash[32];
    ec = nm_dtls_get_fingerprint_from_private_key(dev->privateKey, hash);
    if (ec == NABTO_EC_OK) {
        *fingerprint = toHex(hash, 16);
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_get_device_fingerprint_full_hex(NabtoDevice* device, char** fingerprint)
{
    *fingerprint = NULL;
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (dev->privateKey == NULL) {
        ec = NABTO_DEVICE_EC_INVALID_STATE;
    }
    uint8_t hash[32];
    ec = nm_dtls_get_fingerprint_from_private_key(dev->privateKey, hash);
    if (ec == NABTO_EC_OK) {
        *fingerprint = toHex(hash, 32);
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);
}

NABTO_DEVICE_DECL_PREFIX NabtoDeviceError NABTO_DEVICE_API
nabto_device_connection_get_client_fingerprint_hex(NabtoDevice* device, NabtoDeviceConnectionRef connectionRef, char** fp)
{
    *fp = NULL;
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);

    uint8_t clientFingerprint[32];

    struct nc_client_connection* connection = nc_device_connection_from_ref(&dev->core, connectionRef);

    if (connection == NULL || nc_client_connection_get_client_fingerprint(connection, clientFingerprint) != NABTO_EC_OK) {
        ec = NABTO_EC_INVALID_CONNECTION;
    } else {
        *fp = toHex(clientFingerprint, 16);
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}

NABTO_DEVICE_DECL_PREFIX NabtoDeviceError NABTO_DEVICE_API
nabto_device_connection_get_client_fingerprint_full_hex(NabtoDevice* device, NabtoDeviceConnectionRef connectionRef, char** fp)
{
    *fp = NULL;
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);

    uint8_t clientFingerprint[32];

    struct nc_client_connection* connection = nc_device_connection_from_ref(&dev->core, connectionRef);

    if (connection == NULL || nc_client_connection_get_client_fingerprint(connection, clientFingerprint) != NABTO_EC_OK) {
        ec = NABTO_EC_INVALID_CONNECTION;
    } else {
        *fp = toHex(clientFingerprint, 32);
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}



/**
 * Closing the device
 */
void nabto_device_close_cb(const np_error_code ec, void* data)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)data;
    nabto_device_future_resolve(dev->closeFut, nabto_device_error_core_to_api(ec));
}

void NABTO_DEVICE_API nabto_device_close(NabtoDevice* device, NabtoDeviceFuture* future)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    struct nabto_device_future* fut = (struct nabto_device_future*)future;
    nabto_device_future_reset(fut);

    nabto_device_threads_mutex_lock(dev->eventMutex);
    dev->closeFut = fut;
    np_error_code ec = nc_device_close(&dev->core, &nabto_device_close_cb, dev);
    if (ec != NABTO_EC_OK) {
        nabto_device_future_resolve(fut, ec);
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
}


NabtoDeviceError NABTO_DEVICE_API nabto_device_set_log_callback(NabtoDevice* device, NabtoDeviceLogCallback cb, void* data)
{
    nm_api_logging_set_callback(cb, data);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_log_level(NabtoDevice* device, const char* level)
{
    uint32_t l = 0;
    if (strcmp(level, "error") == 0) {
        l = NABTO_LOG_SEVERITY_LEVEL_ERROR;
    } else if (strcmp(level, "warn") == 0) {
        l = NABTO_LOG_SEVERITY_LEVEL_WARN;
    } else if (strcmp(level, "info") == 0) {
        l = NABTO_LOG_SEVERITY_LEVEL_INFO;
    } else if (strcmp(level, "trace") == 0) {
        l = NABTO_LOG_SEVERITY_LEVEL_TRACE;
    } else {
        return NABTO_DEVICE_EC_INVALID_ARGUMENT;
    }
    nm_api_logging_set_level(l);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API nabto_device_set_log_std_out_callback(NabtoDevice* device)
{
    nm_api_logging_set_callback(&nm_api_logging_std_out_callback, NULL);
    return NABTO_DEVICE_EC_OK;
}

NabtoDeviceError NABTO_DEVICE_API
nabto_device_add_server_connect_token(NabtoDevice* device, const char* serverConnectToken)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    ec = nc_device_add_server_connect_token(&dev->core, serverConnectToken);
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);
}

NabtoDeviceError NABTO_DEVICE_API
nabto_device_are_server_connect_tokens_synchronized(NabtoDevice* device)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    np_error_code ec;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    ec = nc_device_is_server_connect_tokens_synchronized(&dev->core);
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return nabto_device_error_core_to_api(ec);
}

NabtoDeviceError NABTO_DEVICE_API
nabto_device_create_server_connect_token(NabtoDevice* device, char** serverConnectToken)
{
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrtsuvwxyz0123456789";
    size_t alphabetLength = strlen(alphabet);

    struct nabto_device_context* dev = (struct nabto_device_context*)device;

    np_error_code ec;
    nabto_device_threads_mutex_lock(dev->eventMutex);

    struct np_platform* pl = &dev->pl;

    char output[21];
    memset(output, 0, 21);
    size_t generated = 0;
    while (generated < 20) {
        uint8_t randByte;

        ec = pl->random.random(pl, &randByte, 1);
        if (ec) {
            break;
        }
        if (randByte < alphabetLength) {
            output[generated] = alphabet[randByte];
            generated++;
        }
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
    if (ec == NABTO_EC_OK) {
        *serverConnectToken = strdup(output);
    }
    return nabto_device_error_core_to_api(ec);
}

/*
 * Thread running the network
 */
void* nabto_device_network_thread(void* data)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)data;
    int nfds;
    while(true) {
        nfds = nabto_device_platform_inf_wait();
        nabto_device_threads_mutex_lock(dev->eventMutex);
        nabto_device_platform_read(nfds);
        nabto_device_threads_cond_signal(dev->eventCond);
        if (dev->closing && nabto_device_platform_finished()) {
            nabto_device_threads_mutex_unlock(dev->eventMutex);
            return NULL;
        }
        nabto_device_threads_mutex_unlock(dev->eventMutex);
    }
    return NULL;
}

/*
 * Thread running the core
 */
void* nabto_device_core_thread(void* data)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)data;
    while (true) {
        bool end = false;
        nabto_device_threads_mutex_lock(dev->eventMutex);
        np_event_queue_execute_all(&dev->pl);
        nabto_device_threads_mutex_unlock(dev->eventMutex);

        nabto_api_future_queue_execute_all(dev);

        nabto_device_threads_mutex_lock(dev->eventMutex);
        if (np_event_queue_has_ready_event(&dev->pl)) {
            NABTO_LOG_TRACE(LOG, "future execution added events, not waiting");
        } else if (!nabto_api_future_queue_is_empty(dev)) {
            // Not waiting
        } else if (np_event_queue_has_timed_event(&dev->pl)) {
            uint32_t ms = np_event_queue_next_timed_event_occurance(&dev->pl);
            nabto_device_threads_cond_timed_wait(dev->eventCond, dev->eventMutex, ms);
        } else if (dev->closing &&
                   np_event_queue_is_event_queue_empty(&dev->pl) &&
                   !np_event_queue_has_timed_event(&dev->pl) &&
                   nabto_api_future_queue_is_empty(dev))
        {
            end = true;
        } else {
            NABTO_LOG_TRACE(LOG, "no timed events, waits for signals forever");
            nabto_device_threads_cond_wait(dev->eventCond, dev->eventMutex);
        }

        nabto_device_threads_mutex_unlock(dev->eventMutex);

        if (end) {
            return NULL;
        }
    }

    return NULL;
}

void nabto_device_free_threads(struct nabto_device_context* dev)
{
    if (dev->coreThread) {
        nabto_device_threads_free_thread(dev->coreThread);
        dev->coreThread = NULL;
    }
    if (dev->networkThread) {
        nabto_device_threads_free_thread(dev->networkThread);
        dev->networkThread = NULL;
    }
    if (dev->eventMutex) {
        nabto_device_threads_free_mutex(dev->eventMutex);
        dev->eventMutex = NULL;
    }
    if (dev->eventCond) {
        nabto_device_threads_free_cond(dev->eventCond);
        dev->eventCond = NULL;
    }
    if (dev->futureQueueMutex) {
        nabto_device_threads_free_mutex(dev->futureQueueMutex);
        dev->futureQueueMutex = NULL;
    }
}
