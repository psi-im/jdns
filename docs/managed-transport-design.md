# Managed DNS transports in QJDns

## Existing implementation

JDNS (C) owns DNS query encoding, response decoding, SRV/CNAME parsing, caching
and TTLs. QJDns is the Qt event-loop integration. The old Qt transport was a
private `QJDnsTransport` interface with UDP on desktop. Android implemented
`ManagedUnicast`: requests are whole DNS wire packets, destination selection
and retry policy move to the transport, and the transport returns whole DNS
wire responses. Android already contains private DNS-over-TCP framing and
DNS-over-TLS code for platform DNS.

No TCP or HTTP stack belongs in the JDNS C parser.

## Proposed API and ownership

Each QJDns instance uses exactly one transport; **transport classes are
named by the protocol/backend, not by the server's location**. There is no
local-vs-remote distinction: UDP, TCP and DoH servers can be on either host.

- `QJDns::setTransport(QJDnsTransport *)` transfers ownership to QJDns **before**
  calling `init()`; with no transport supplied, existing UDP/Android behavior
  remains unchanged.
- `QJDnsTcpTransport(server, port, QNetworkProxy)` uses DNS-over-TCP
  (length-prefixed RFC 7766 frames). The caller supplies an explicit Qt
  proxy, including `NoProxy` when direct TCP is intended.
- `QJDnsDohTransport(endpoint, QNetworkAccessManager *)` performs RFC 8484
  DNS-over-HTTPS. The manager is **not owned** by JDNS and must already be
  configured with the required proxy, TLS and authentication policies.
- Both concrete transports reuse the request bookkeeping, timeout and
  cancellation logic of `QJDnsExternalTransport` internally rather than
  duplicating DNS parsing, packet IDs and retry logic.
- `QJDnsExternalTransport` is a separate, **HTTP-agnostic** implementation.
  It emits `queryRequested(requestId, dnsWirePacket)`. A caller may send that
  packet using its own HTTP/TLS stack, libcurl, an application
  network service or another transport, then report completion with
  `completeRequest(requestId, responseWirePacket)` or failure with
  `failRequest(requestId, message)`.
  The caller's request IDs are separate from DNS transaction IDs and are not
  reused. `requestCancelled(requestId)` asks the caller to abort the external
  network job when a handle closes or a timeout occurs. The caller must keep
  its backend alive and process callbacks on the transport's QObject thread.
  The transport synthesizes a DNS SERVFAIL after failure/timeout (never local
  DNS), and ignores late completion after cancellation.
- The transport is scoped to a single QJDns session. Separate connections
  can have separate DNS privacy policies rather than sharing a global resolver.
- All three explicit transports are asynchronous, support concurrent wire queries, accept
  cancellation by closing the handle, have per-request timeouts and deliver
  responses back to JDNS.

## Privacy contract

1. When a proxy-specific resolver is selected, the caller supplies an explicit
   proxy for **both** DNS and destination CONNECT (and supplies a configured
   HTTP manager for DoH). No fallback to local system DNS.
2. The `QJDnsTcpTransport` calls QTcpSocket::connectToHost(server, port) with its
   caller-supplied QNetworkProxy. To avoid local hostname resolution, use
   SOCKS5/HTTP proxy configured for hostname CONNECT; verify this in a packet
   capture on each supported Qt version.
3. The `QJDnsDohTransport` endpoint must be HTTPS. Redirects are not followed. The calling
   application must configure the HTTP manager's proxy and TLS handling
   correctly. The DoH endpoint hostname is passed through that manager; there
   is no separate local lookup inside JDNS.
4. The external backend is responsible for routing and applying DoH HTTP
   policy (HTTPS URL, Content-Type, TLS verification, proxy) or any equivalent
   custom transport. JDNS cannot enforce those network properties for an
   opaque external backend. It still enforces wire-response size/transaction ID
   and provides no local DNS fallback.
5. If the DNS transport fails or times out, deliver a matching SERVFAIL to
   JDNS. Do not retry over UDP, system DNS, or a different network path.
6. JDNS accepts decoded records **only** through its existing parser;
   the new transport performs wire framing and lightweight ID matching.

## Tests to retain

- DNS-over-TCP length-prefixed framing, including split frames.
- DoH POST with RFC 8484 `application/dns-message` body/header.
- Invalid DNS IDs, truncated answers, oversized frames, TCP close and timeout.
- DoH HTTP failures, redirects and TLS errors propagate as DNS errors; no
  implicit network fallback.
- Concurrent queries and request cancellation (including late completions).
- External backend uses the real JDNS DNS parser and propagates failures
  without QNetworkAccessManager or any local-DNS fallback.
- No multicast/Android regression and no required Qt GUI components.
