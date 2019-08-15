#include <modules/dtls/nm_dtls_cli.h>
#include <modules/dtls/nm_dtls_srv.h>
#include <modules/dns/unix/nm_unix_dns.h>
#include <modules/timestamp/unix/nm_unix_timestamp.h>
#include <modules/udp/select_unix/nm_select_unix.h>

void test_platform_init_modules(struct test_platform* tp)
{
    struct np_platform* pl = &tp->pl;
    np_communication_buffer_init(pl);
    nm_unix_udp_select_init(pl);
    nm_unix_ts_init(pl);
    nm_unix_dns_init(pl);
    nm_dtls_cli_init(pl);
    nm_dtls_srv_init(pl);
}