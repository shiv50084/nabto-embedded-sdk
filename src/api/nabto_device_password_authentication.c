#include "nabto_device_password_authentication.h"
#include <nabto/nabto_device_experimental.h>
#include <api/nabto_device_threads.h>
#include <api/nabto_device_defines.h>
#include <api/nabto_device_event_handler.h>
#include <api/nabto_device_error.h>

#include <core/nc_spake2.h>

/**
 * Handler which is registered in the core to handle new password requests.
 */
static np_error_code password_request_handler(struct nc_spake2_password_request* req, void* data);


np_error_code nabto_device_password_authentication_listener_resolve_event(const np_error_code ec, struct nabto_device_future* future, void* eventData, void* listenerData)
{
    if (ec == NABTO_EC_OK) {
        // The item in eventData needs to be converted to data on the future.
        // eventData is a struct nabto_device_password_authentication_request

        // this uses the generic future resolve data approach.
    } else if (ec == NABTO_EC_OUT_OF_MEMORY) {
        // ok dont care, password auth request returns 500.
    } else if (ec == NABTO_EC_ABORTED) {
        struct nabto_device_context* dev = future->dev;
        nc_spake2_clear_password_request_callback(&dev->core.spake2);

        // the listener has been freed
        // dunno what to do
    } else if (ec == NABTO_EC_STOPPED) {
        struct nabto_device_context* dev = future->dev;
        nc_spake2_clear_password_request_callback(&dev->core.spake2);
        //nc_spake2_set_password_request_callback(&dev->core.spake2, NULL, NULL);
    }
    return NABTO_EC_OK;
}

bool NABTO_DEVICE_API
nabto_device_connection_is_password_authenticated(NabtoDevice* device, NabtoDeviceConnectionRef ref)
{
    // find the connection
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    bool passwordAuthenticated = false;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    struct nc_client_connection* connection = nc_device_connection_from_ref(&dev->core, ref);
    if (connection != NULL) {
        passwordAuthenticated = nc_client_connection_is_password_authenticated(connection);
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return passwordAuthenticated;
}

NabtoDeviceError NABTO_DEVICE_API
nabto_device_password_authentication_request_init_listener(NabtoDevice* device, NabtoDeviceListener* passwordAuthenticationListener)
{
    struct nabto_device_context* dev = (struct nabto_device_context*)device;
    struct nabto_device_listener* listener = (struct nabto_device_listener*)passwordAuthenticationListener;
    np_error_code ec = NABTO_EC_OK;

    nabto_device_threads_mutex_lock(dev->eventMutex);

    if (dev->core.spake2.passwordRequest != NULL) {
        ec = NABTO_EC_IN_USE;
    } else {



        ec = nabto_device_listener_init(dev, listener, NABTO_DEVICE_LISTENER_TYPE_PASSWORD_REQUESTS, &nabto_device_password_authentication_listener_resolve_event, NULL);

        if (ec == NABTO_EC_OK) {
            nc_spake2_set_password_request_callback(&dev->core.spake2, password_request_handler, listener);
        }
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);

    return nabto_device_error_core_to_api(ec);
}

np_error_code password_request_handler(struct nc_spake2_password_request* req, void* data)
{
    struct nabto_device_password_authentication_request* r = calloc(1, sizeof(struct nabto_device_password_authentication_request));
    if (r == NULL) {
        return NABTO_EC_OUT_OF_MEMORY;
    }
    struct nabto_device_listener* listener = data;
    r->passwordRequest = req;

    np_error_code ec = nabto_device_listener_add_event(listener, &r->eventListNode, r);
    return ec;
}

const char* NABTO_DEVICE_API
nabto_device_password_authentication_request_get_username(NabtoDevicePasswordAuthenticationRequest* request)
{
    struct nabto_device_password_authentication_request* req = (struct nabto_device_password_authentication_request*)request;
    struct nabto_device_context* dev = req->device;
    struct nc_spake2_password_request* passwordRequest = req->passwordRequest;
    const char* response;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (req->handled) {
        response = NULL;
    } else {
        response = passwordRequest->username;
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return response;
}

/**
 * Set password for the user.
 */
NabtoDeviceError NABTO_DEVICE_API
nabto_device_password_authentication_request_set_password(NabtoDevicePasswordAuthenticationRequest* request, const char* password)
{
    struct nabto_device_password_authentication_request* req = (struct nabto_device_password_authentication_request*)request;
    struct nabto_device_context* dev = req->device;
    struct nc_spake2_password_request* passwordRequest = req->passwordRequest;
    NabtoDeviceError ec = NABTO_DEVICE_EC_OK;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (req->handled) {
        ec = NABTO_DEVICE_EC_INVALID_STATE;
    } else {
        nc_spake2_password_ready(passwordRequest, password);
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    return ec;
}



/**
 * Free a password authentication request.
 */
void NABTO_DEVICE_API nabto_device_password_authentication_request_free(NabtoDevicePasswordAuthenticationRequest* request)
{
    struct nabto_device_password_authentication_request* req = (struct nabto_device_password_authentication_request*)request;
    struct nabto_device_context* dev = req->device;
    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (!req->handled) {
        nc_spake2_password_ready(req->passwordRequest, NULL);
    }
    nabto_device_threads_mutex_unlock(dev->eventMutex);
    free(req);
}


void NABTO_DEVICE_API
nabto_device_listener_new_password_authentication_request(NabtoDeviceListener* passwordAuthenticationListener, NabtoDeviceFuture* future, NabtoDevicePasswordAuthenticationRequest** request)
{
    struct nabto_device_listener* listener = (struct nabto_device_listener*)passwordAuthenticationListener;
    struct nabto_device_context* dev = listener->dev;
    struct nabto_device_future* fut = (struct nabto_device_future*)future;
    nabto_device_future_reset(fut);

    nabto_device_threads_mutex_lock(dev->eventMutex);
    if (nabto_device_listener_get_type(listener) != NABTO_DEVICE_LISTENER_TYPE_PASSWORD_REQUESTS) {
        nabto_device_threads_mutex_unlock(dev->eventMutex);
        return nabto_device_future_resolve(fut, NABTO_DEVICE_EC_INVALID_ARGUMENT);
    }

    np_error_code ec = nabto_device_listener_get_status(listener);
    if (ec != NABTO_EC_OK) {
        nabto_device_threads_mutex_unlock(dev->eventMutex);
        return nabto_device_future_resolve(fut, nabto_device_error_core_to_api(ec));
    }

    ec = nabto_device_listener_init_future(listener, fut);
    if (ec != NABTO_EC_OK) {
        nabto_device_future_resolve(fut, ec);
    } else {
        *request = NULL;
        listener->genericFutureResolverData = (void**)request;
        nabto_device_listener_try_resolve(listener);
    }

    nabto_device_threads_mutex_unlock(dev->eventMutex);
}
