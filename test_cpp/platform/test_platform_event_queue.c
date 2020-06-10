#include "test_platform_event_queue.h"


#include "test_platform_event_queue.h"

#include <platform/np_logging.h>
#include <platform/interfaces/np_event_queue.h>
#include <platform/np_platform.h>

#include <stdlib.h>

#include <event.h>
#include <event2/event.h>

#define LOG NABTO_LOG_MODULE_EVENT_QUEUE

static np_error_code create_event(struct np_event_queue* obj, np_event_callback cb, void* cbData, struct np_event** event);
static void destroy_event(struct np_event* event);
static void post(struct np_event* event);
static void post_maybe_double(struct np_event* event);
static void post_timed_event(struct np_event* event, uint32_t milliseconds);
static void cancel(struct np_event* event);

struct test_platform_event_queue {
    struct event_base* eventBase;
};

struct np_event {
    struct test_platform_event_queue* queue;
    np_event_callback cb;
    void* data;
    struct event event;
};

static struct np_event_queue_functions vtable = {
    .create = &create_event,
    .destroy = &destroy_event,
    .post = &post,
    .post_maybe_double = &post_maybe_double,
    .post_timed = &post_timed_event,
    .cancel = &cancel
};

struct test_platform_event_queue* test_platform_event_queue_init(struct event_base* eventBase)
{
    struct test_platform_event_queue* eq = calloc(1, sizeof(struct test_platform_event_queue));
    eq->eventBase = eventBase;
    return eq;
}

struct np_event_queue test_platform_event_queue_get_impl(struct test_platform_event_queue* eq)
{
    struct np_event_queue queue;
    queue.vptr = &vtable;
    queue.data = eq;
    return queue;
}

void test_platform_event_queue_deinit(struct test_platform_event_queue* eq)
{
    event_base_loopbreak(eq->eventBase);
    free(eq);
}

void handle_event(evutil_socket_t s, short events, void* data)
{
//    NABTO_LOG_TRACE(LOG, "handle event");
    struct np_event* event = data;
//    struct np_platform* pl = event->pl;
//    struct test_platform_event_queue* eq = pl->eqData;

//    nabto_device_threads_mutex_lock(eq->mutex);
    event->cb(event->data);
//    nabto_device_threads_mutex_unlock(eq->mutex);
}

np_error_code create_event(struct np_event_queue* obj, np_event_callback cb, void* cbData, struct np_event** event)
{
    struct np_event* ev = calloc(1, sizeof(struct np_event));
    struct test_platform_event_queue* eq = obj->data;
    ev->queue = eq;
    ev->cb = cb;
    ev->data = cbData;
    event_assign(&ev->event, eq->eventBase, -1, 0, &handle_event, ev);

    *event = ev;
    return NABTO_EC_OK;

}

void destroy_event(struct np_event* event)
{
    free(event);
}

void post(struct np_event* event)
{
    //NABTO_LOG_TRACE(LOG, "post event");
    //struct np_platform* pl = event->pl;
    //struct nabto_device_event_queue* eq = pl->eqData;
    event_active(&event->event, 0, 0);
}

void post_maybe_double(struct np_event* event)
{
    // TODO
    //struct np_platform* pl = event->pl;
    //struct nabto_device_event_queue* eq = pl->eqData;
    event_active(&event->event, 0, 0);
}

void post_timed_event(struct np_event* event, uint32_t milliseconds)
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
