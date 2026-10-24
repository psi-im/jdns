/*
 * Multicast DNS isolation/regression tests. No sockets or Qt are needed.
 * SPDX-License-Identifier: MIT
 */
#include "jdns.h"
#ifdef NDEBUG
#undef NDEBUG // Test invariants must remain active in CMake Release builds.
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int now;
    int seed;
    int binds;
    int unbinds;
    int sends;
    int reads;
    int fail_writes;
    int last_destination_port;
    int bind_multicast;
    int received_length;
    unsigned char received[1024];
    unsigned char sent[9000];
    int sent_length;
} FakeNetwork;

static int clock_now(jdns_session_t *s, void *app)
{
    (void)s;
    return ((FakeNetwork *)app)->now;
}
static int random_number(jdns_session_t *s, void *app)
{
    (void)s;
    return ++((FakeNetwork *)app)->seed;
}
static void debug_log(jdns_session_t *s, void *app, const char *line)
{
    (void)s; (void)app; (void)line;
}
static int bind_udp(jdns_session_t *s, void *app, const jdns_address_t *address,
                    int port, const jdns_address_t *multicast)
{
    FakeNetwork *f = (FakeNetwork *)app;
    (void)s; (void)address;
    assert(port == 5353);
    ++f->binds;
    if(multicast)
        ++f->bind_multicast;
    return 7;
}
static void unbind_udp(jdns_session_t *s, void *app, int handle)
{
    (void)s;
    assert(handle == 7);
    ++((FakeNetwork *)app)->unbinds;
}
static int receive_udp(jdns_session_t *s, void *app, int handle,
                       jdns_address_t *address, int *port,
                       unsigned char *buf, int *size)
{
    FakeNetwork *f = (FakeNetwork *)app;
    (void)s;
    assert(handle == 7);
    ++f->reads;
    if(!f->received_length)
        return 0;
    assert(*size >= f->received_length);
    memcpy(buf, f->received, (size_t)f->received_length);
    *size = f->received_length;
    f->received_length = 0;
    jdns_address_set_ipv4(address, 0x7f000001);
    *port = 5353;
    return 1;
}
static int transmit_udp(jdns_session_t *s, void *app, int handle,
                        const jdns_address_t *address, int port,
                        unsigned char *packet, int size)
{
    FakeNetwork *f = (FakeNetwork *)app;
    (void)s;
    assert(handle == 7 && address != NULL);
    assert(size >= 12 && size <= (int)sizeof(f->sent));
    if(f->fail_writes) {
        --f->fail_writes;
        return 0;
    }
    memcpy(f->sent, packet, (size_t)size);
    f->sent_length = size;
    f->last_destination_port = port;
    ++f->sends;
    return 1;
}
static jdns_session_t *new_multicast(FakeNetwork *f)
{
    jdns_callbacks_t common = {0};
    jdns_datagram_callbacks_t io = {0};
    jdns_session_t *s;
    jdns_address_t *address, *group;
    common.app = f;
    common.time_now = clock_now;
    common.rand_int = random_number;
    common.debug_line = debug_log;
    s = jdns_session_new(&common);
    assert(s);
    io.transport_bind = bind_udp;
    io.transport_unbind = unbind_udp;
    io.transport_read = receive_udp;
    io.transport_write = transmit_udp;
    address = jdns_address_new();
    jdns_address_set_ipv4(address, 0);
    group = jdns_address_multicast4_new();
    assert(jdns_init_multicast(s, &io, address, 5353, group));
    jdns_address_delete(group);
    jdns_address_delete(address);
    assert(f->binds == 1 && f->bind_multicast == 1);
    return s;
}
static void pump(jdns_session_t *s, FakeNetwork *f, int count)
{
    int n;
    for(n = 0; n < count; ++n) {
        f->now += 300;
        (void)jdns_step(s);
    }
}
static jdns_rr_t *new_ptr_record(void)
{
    jdns_rr_t *r = jdns_rr_new();
    jdns_rr_set_owner(r, (const unsigned char *)"_demo._tcp.local.");
    jdns_rr_set_PTR(r, (const unsigned char *)"sample._demo._tcp.local.");
    r->ttl = 120;
    assert(jdns_rr_verify(r));
    return r;
}
static jdns_event_t *next_event(jdns_session_t *s)
{
    return jdns_next_event(s);
}
static void publication_lifecycle(void)
{
    FakeNetwork f = {0};
    jdns_session_t *s = new_multicast(&f);
    jdns_rr_t *rr = new_ptr_record();
    jdns_event_t *event;
    int first = jdns_publish(s, JDNS_PUBLISH_SHARED, rr);
    assert(first >= 0);
    event = next_event(s);
    assert(event && event->type == JDNS_EVENT_PUBLISH &&
           event->id == first && event->status == JDNS_STATUS_SUCCESS);
    jdns_event_delete(event);

    /* A duplicate type+name must be rejected and report a typed error. */
    {
        int duplicate = jdns_publish(s, JDNS_PUBLISH_SHARED, rr);
        event = next_event(s);
        assert(event && event->type == JDNS_EVENT_PUBLISH &&
               event->id == duplicate && event->status == JDNS_STATUS_ERROR);
        jdns_event_delete(event);
    }
    pump(s, &f, 20);
    assert(f.sends > 0);
    assert(f.last_destination_port == 5353);
    assert(f.sent_length >= 12);
    jdns_update_publish(s, first, rr);
    jdns_cancel_publish(s, first);
    jdns_rr_delete(rr);
    jdns_session_delete(s);
    assert(f.unbinds == 1);
}
static void query_reuse_cancel_and_probe(void)
{
    FakeNetwork f = {0};
    jdns_session_t *s = new_multicast(&f);
    int a = jdns_query(s, (const unsigned char *)"_demo._tcp.local.", JDNS_RTYPE_PTR);
    int b = jdns_query(s, (const unsigned char *)"_demo._tcp.local.", JDNS_RTYPE_PTR);
    assert(a != b);
    pump(s, &f, 20);
    assert(f.sends >= 1);
    jdns_cancel_query(s, a);
    /* The second observer is still subscribed. */
    jdns_probe(s);
    pump(s, &f, 20);
    jdns_cancel_query(s, b);
    jdns_session_delete(s);
    assert(f.unbinds == 1);
}
static void malformed_multicast_and_shutdown(void)
{
    FakeNetwork f = {0};
    jdns_session_t *s = new_multicast(&f);
    jdns_event_t *event = NULL;
    /* Malformed datagram must be ignored, not delivered as an answer. */
    f.received[0] = 0x42;
    f.received_length = 1;
    jdns_set_handle_readable(s, 7);
    pump(s, &f, 1);
    assert(f.reads >= 1);
    assert(next_event(s) == NULL);
    jdns_shutdown(s);
    pump(s, &f, 20);
    while((event = next_event(s)) != NULL) {
        int shutdown = event->type == JDNS_EVENT_SHUTDOWN;
        jdns_event_delete(event);
        if(shutdown)
            break;
    }
    assert(event != NULL);
    jdns_session_delete(s);
    assert(f.unbinds == 1);
}
static void temporary_output_backpressure(void)
{
    FakeNetwork f = {0};
    jdns_session_t *s = new_multicast(&f);
    jdns_rr_t *rr = new_ptr_record();
    jdns_event_t *event;
    int id = jdns_publish(s, JDNS_PUBLISH_SHARED, rr);
    event = next_event(s);
    assert(event && event->status == JDNS_STATUS_SUCCESS);
    jdns_event_delete(event);
    f.fail_writes = 1;
    pump(s, &f, 12);
    /* A writable notification must allow queued sends to resume. */
    jdns_set_handle_writable(s, 7);
    pump(s, &f, 20);
    assert(f.sends > 0);
    jdns_cancel_publish(s, id);
    jdns_rr_delete(rr);
    jdns_session_delete(s);
}
int main(void)
{
    publication_lifecycle();
    query_reuse_cancel_and_probe();
    malformed_multicast_and_shutdown();
    temporary_output_backpressure();
    puts("mDNS isolation regression tests passed");
    return 0;
}
