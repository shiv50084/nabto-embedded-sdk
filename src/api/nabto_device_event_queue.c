#include "nabto_device_event_queue.h"

#include "nabto_device_threads.h"
#include "nabto_device_future.h"

#include <platform/np_logging.h>

#include <stdlib.h>

#include <event2/event.h>

#define LOG NABTO_LOG_MODULE_EVENT_QUEUE

static np_error_code create_event(struct np_platform* pl, np_event_callback cb, void* data, struct np_event** event);
static void destroy_event(struct np_event* event);
static bool post(struct np_event* event);
static void post_maybe_double(struct np_event* event);

static np_error_code create_timed_event(struct np_platform* pl, np_timed_event_callback cb, void* data, struct np_timed_event** event);
static void destroy_timed_event(struct np_timed_event* event);

static void post_timed_event(struct np_timed_event* event, uint32_t milliseconds);
static void cancel(struct np_event* event);
static void cancel_timed_event(struct np_timed_event* timedEvent);

static void handle_timed_event(evutil_socket_t s, short events, void* data);
static void handle_event(evutil_socket_t s, short events, void* data);

static void* nabto_device_event_queue_core_thread(void* data);

struct nabto_device_event_queue {
    struct nabto_device_mutex* mutex;
    struct nabto_device_thread* coreThread;
    struct event_base* eventBase;
};

struct np_event {
    struct np_platform* pl;
    np_event_callback cb;
    void* data;
    struct event event;
};

struct np_timed_event {
    struct np_platform* pl;
    np_timed_event_callback cb;
    void* data;
    struct event event;
};

void nabto_device_event_queue_init(struct np_platform* pl, struct nabto_device_mutex* mutex)
{
    struct nabto_device_event_queue* eq = calloc(1, sizeof(struct nabto_device_event_queue));

    eq->mutex = mutex;
    eq->eventBase = event_base_new();
    pl->eqData = eq;

    eq->coreThread = nabto_device_threads_create_thread();
    nabto_device_threads_run(eq->coreThread, nabto_device_event_queue_core_thread, eq);

    pl->eq.create_event = &create_event;
    pl->eq.destroy_event = &destroy_event;
    pl->eq.post = &post;
    pl->eq.post_maybe_double = &post_maybe_double;

    pl->eq.create_timed_event = &create_timed_event;
    pl->eq.destroy_timed_event = &destroy_timed_event;
    pl->eq.post_timed_event = &post_timed_event;
    pl->eq.cancel = &cancel;
    pl->eq.cancel_timed_event = &cancel_timed_event;
}

void nabto_device_event_queue_deinit(struct np_platform* pl)
{
    struct nabto_device_event_queue* eq = pl->eqData;
    event_base_loopbreak(eq->eventBase);
    nabto_device_threads_join(eq->coreThread);
    event_base_free(eq->eventBase);
    nabto_device_threads_free_thread(eq->coreThread);
    free(eq);
}

void handle_timed_event(evutil_socket_t s, short events, void* data)
{
    NABTO_LOG_TRACE(LOG, "handle timed event");
    struct np_timed_event* timedEvent = data;
    struct np_platform* pl = timedEvent->pl;
    struct nabto_device_event_queue* eq = pl->eqData;

    nabto_device_threads_mutex_lock(eq->mutex);
    timedEvent->cb(NABTO_EC_OK, timedEvent->data);
    nabto_device_threads_mutex_unlock(eq->mutex);

}

void handle_event(evutil_socket_t s, short events, void* data)
{
    NABTO_LOG_TRACE(LOG, "handle event");
    struct np_event* event = data;
    struct np_platform* pl = event->pl;
    struct nabto_device_event_queue* eq = pl->eqData;

    nabto_device_threads_mutex_lock(eq->mutex);
    event->cb(event->data);
    nabto_device_threads_mutex_unlock(eq->mutex);
}

void handle_future_event(evutil_socket_t s, short events, void* data)
{
    struct nabto_device_future* future = data;
    nabto_device_future_popped(future);
}

np_error_code create_event(struct np_platform* pl, np_event_callback cb, void* data, struct np_event** event)
{
    struct np_event* ev = calloc(1, sizeof(struct np_event));
    if (ev == NULL) {
        return NABTO_EC_OUT_OF_MEMORY;
    }
    ev->pl = pl;
    ev->cb = cb;
    ev->data = data;

    struct nabto_device_event_queue* eq = pl->eqData;
    event_assign(&ev->event, eq->eventBase, -1, 0, &handle_event, ev);

    *event = ev;
    return NABTO_EC_OK;
}

void destroy_event(struct np_event* event)
{
    free(event);
}

bool post(struct np_event* event)
{
    NABTO_LOG_TRACE(LOG, "post event");
    //struct np_platform* pl = event->pl;
    //struct nabto_device_event_queue* eq = pl->eqData;
    event_active(&event->event, 0, 0);
    return true;
}

void post_maybe_double(struct np_event* event)
{
    // TODO
    //struct np_platform* pl = event->pl;
    //struct nabto_device_event_queue* eq = pl->eqData;
    event_active(&event->event, 0, 0);
}

np_error_code create_timed_event(struct np_platform* pl, np_timed_event_callback cb, void* data, struct np_timed_event** event)
{
    struct np_timed_event* ev = calloc(1, sizeof(struct np_timed_event));
    if (ev == NULL) {
        return NABTO_EC_OUT_OF_MEMORY;
    }
    struct nabto_device_event_queue* eq = pl->eqData;
    ev->pl = pl;
    ev->cb = cb;
    ev->data = data;

    event_assign(&ev->event, eq->eventBase, -1, 0, &handle_timed_event, ev);

    *event = ev;
    return NABTO_EC_OK;
}

void destroy_timed_event(struct np_timed_event* event)
{
    free(event);
}

void post_timed_event(struct np_timed_event* event, uint32_t milliseconds)
{
    //struct np_platform* pl = event->pl;
    //struct nabto_device_event_queue* eq = pl->eqData;
    struct timeval tv;
    tv.tv_sec = (milliseconds / 1000);
    tv.tv_usec = ((milliseconds % 1000) * 1000);
    event_add (&event->event, &tv);
}

void cancel(struct np_event* event)
{
    event_del(&event->event);
}

void cancel_timed_event(struct np_timed_event* timedEvent)
{
    event_del(&timedEvent->event);
}


void nabto_device_event_queue_future_post(struct np_platform* pl, struct nabto_device_future* fut)
{
    struct nabto_device_event_queue* eq = pl->eqData;
    event_assign(&fut->event, eq->eventBase, -1, 0, &handle_future_event, fut);
    event_active(&fut->event, 0, 0);
}

void* nabto_device_event_queue_core_thread(void* data)
{
    struct nabto_device_event_queue* eq = data;
    event_base_loop(eq->eventBase, EVLOOP_NO_EXIT_ON_EMPTY);
    return NULL;
}