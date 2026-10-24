# JDNS

JDNS is an event-loop-friendly DNS library with a dependency-free C protocol
engine and a Qt wrapper, **QJDns**, for Qt 5 and Qt 6. It supports traditional
DNS resolution and multicast DNS (mDNS), including publishing records on the
local network. Originally created by Justin Karneges in 2005, it is licensed
under the MIT license.

## What it can do

| Capability | Details |
| --- | --- |
| Unicast DNS | Asynchronous queries, including A, AAAA, SRV, MX, TXT, PTR, CNAME and other DNS record types (unknown types can be handled as raw RDATA). |
| DNS resolution engine | DNS wire encoding/decoding, CNAME traversal, TTL caching/expiration and coalescing of identical concurrent queries. |
| Multicast DNS | mDNS queries, ongoing discovery, record publication, updates and conflict handling using the bundled `mdnsd` engine. |
| UDP | Built-in datagram transport for conventional DNS and mDNS. |
| DNS over TCP | `QJDnsTcpTransport` implements DNS-over-TCP framing (RFC 7766), with explicit Qt proxy configuration, including SOCKS5. |
| DNS over HTTPS | `QJDnsHttpTransport` implements RFC 8484 using Qt Network. **HTTPS is required**; plain HTTP and redirects are rejected. Its `QNetworkAccessManager` may be supplied or internally owned. |
| Custom backends | `QJDnsExternalTransport` passes complete DNS wire requests to an application-owned asynchronous backend (for example, a custom HTTP/TLS stack). |
| Android DNS | A platform-specific backend uses the Android system resolver where available, taking account of the platform's network and Private DNS configuration; older Android versions use a fallback resolver implementation. |
| Error and cancellation handling | Distinct transport failures/timeouts, per-request cancellation and no implicit fallback to a different DNS transport after an error. |

The C API supplies callbacks for the application's clock, randomness and
network I/O; it has no Qt dependency. The Qt wrapper supplies the event-loop
integration and the concrete transport implementations.

**Scope:** JDNS is a DNS/mDNS building block, not a complete DNS-SD/Bonjour
service-discovery framework. The application handles service browsing policy,
search-domain expansion and selection between unicast DNS and `.local` mDNS.
**DNS Push Notifications (RFC 8765) are not implemented.** Their planned,
separate, session-oriented design is discussed in
[docs/rfc8765-readiness.md](docs/rfc8765-readiness.md).

## Build

CMake 3.19 or newer is required. The following builds the C library, Qt 6
wrapper and command-line utility (where supported):

```sh
cmake -S . -B build -DBUILD_QJDNS=ON -DJDNS_QT_MAJOR_VERSION=6
cmake --build build --parallel
```

Set `JDNS_QT_MAJOR_VERSION=5` for Qt 5. Set `BUILD_QJDNS=OFF` to build
only the C library, `BUILD_JDNS_TOOL=OFF` to omit the example CLI, or
`BUILD_SHARED_LIBS=ON` to build shared libraries (static is the default).

To run the regression suite:

```sh
cmake -S . -B build -DJDNS_BUILD_TESTS=ON -DJDNS_QT_MAJOR_VERSION=6
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

You can install the libraries and CMake package configuration with
`cmake --install build`.

## Minimal Qt example: resolve an A record via HTTPS

This example sends a DNS request to a public RFC 8484 resolver, prints the
returned IPv4 addresses and terminates the Qt event loop. It uses the new
request-oriented transport API, with no fake socket handles or bind address.

```cpp
// main.cpp
#include <QCoreApplication>
#include <QDebug>
#include <QUrl>

#include "qjdns.h"
#include "qjdns_httptransport.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QJDns dns;

    auto *http = new QJDnsHttpTransport(
        QUrl(QStringLiteral("https://dns.google/dns-query")));
    if (!dns.setQueryTransport(http)) {
        delete http; // Ownership transfers only on success.
        return 1;
    }
    if (!dns.init(QJDns::Unicast))
        return 1;

    QObject::connect(&dns, &QJDns::resultsReady, &app,
                     [&app](int, const QJDns::Response &response) {
        for (const auto &record : response.answerRecords) {
            if (record.type == QJDns::A)
                qInfo().noquote() << record.address.toString();
        }
        app.quit();
    });
    QObject::connect(&dns, &QJDns::error, &app,
                     [&app](int, QJDns::Error error) {
        qWarning() << "DNS lookup failed:" << error;
        app.exit(1);
    });

    if (dns.queryStart("example.org.", QJDns::A) < 0)
        return 1;
    return app.exec();
}
```

For an **installed** JDNS/QJDns package, a minimal consumer
`CMakeLists.txt` using Qt 6 is:

```cmake
cmake_minimum_required(VERSION 3.19)
project(dns_example LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(QJDns CONFIG REQUIRED)

add_executable(dns_example main.cpp)
target_link_libraries(dns_example PRIVATE QJDns::QJDns)
```

When embedding JDNS as a source subdirectory instead, use
`add_subdirectory(path/to/jdns)` and link the same `QJDns::QJDns` target.

The HTTPS URL above is an example **third-party DNS provider**, not a default
server selected by JDNS. Replace it with your own endpoint as needed. Unless
an appropriate proxy or pre-resolved endpoint is configured, Qt may resolve the
HTTPS server's hostname through the OS resolver before opening the HTTPS
connection. No alternate DNS transport is tried if the HTTPS request fails.

### Other transport choices

For ordinary **UDP unicast DNS**, omit `setQueryTransport()`, initialize on
the relevant IP address family, and explicitly provide the DNS servers, for
example:

```cpp
QJDns dns;
QJDns::NameServer server;
server.address = QHostAddress(QStringLiteral("1.1.1.1"));
server.port = 53;

if (!dns.init(QJDns::Unicast, QHostAddress::AnyIPv4) ||
    !dns.setNameServers({server}))
    return 1;

// Connect to resultsReady()/error(), then call queryStart() as above.
```

Use `QJDns::systemInfo().nameServers` to inspect the operating system's
configured resolvers if you explicitly want that policy. For DNS over TCP,
inject `QJDnsTcpTransport(server, port, explicitProxy)` instead; for a
caller-managed HTTP/TLS implementation, use `QJDnsExternalTransport`.
Multicast DNS uses `QJDns::Multicast` and the separate datagram path.

## Using the pure C core

The core API is in `include/jdns/jdns.h`. Create a session with
`jdns_session_new()` and common clock/random/debug callbacks. Initialize
either datagram DNS with `jdns_init_unicast()`, native async unicast with
`jdns_init_async_unicast()`, or mDNS with `jdns_init_multicast()`. Issue
queries through `jdns_query()`, drive timers and events via `jdns_step()`,
and dispose of the session using `jdns_session_delete()`.

The async unicast callback receives a complete DNS wire query and an internal
request ID; the application reports a response through
`jdns_async_complete()` or a typed failure through `jdns_async_fail()`.
Datagram callbacks instead implement actual socket binding, sending and
receiving. The two callback tables are intentionally separate. See the public
header's Doxygen comments for lifetime, threading/reentrancy and cancellation
contracts.
