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

- `QJDns::setTransport(QJDnsTransport *)` transfers ownership to QJDns **before**
  calling `init()`; the default path is unchanged when no transport is injected.
- `QJDnsRemoteTransport::configureTcp(server, port, QNetworkProxy)` uses an
  explicit QTcpSocket proxy configuration. Passing NoProxy is an *explicit*
  choice by the caller, never a fallback.
- `QJDnsRemoteTransport::configureDoh(endpoint, QNetworkAccessManager *)`
  injects the entire HTTP transport. The manager is **not owned** by JDNS.
  The caller sets its proxy (SOCKS5 or HTTP CONNECT), authentication,
  HTTPS TLS trust and policy. JDNS never creates its own HTTP manager.
- The transport is scoped to a single QJDns session. Multiple XMPP accounts
  can have separate DNS privacy policies rather than sharing a global resolver.
- Both modes are asynchronous, support concurrent wire queries, accept
  cancellation by closing the handle, have per-request timeouts and deliver
  responses back to JDNS.

## Privacy contract

1. When a proxy-specific resolver is selected, the caller supplies an explicit
   proxy for **both** DNS and destination CONNECT (and supplies a configured
   HTTP manager for DoH). No fallback to local system DNS.
2. The TCP transport calls QTcpSocket::connectToHost(server, port) with its
   caller-supplied QNetworkProxy. To avoid local hostname resolution, use
   SOCKS5/HTTP proxy configured for hostname CONNECT; verify this in a packet
   capture on each supported Qt version.
3. The DoH endpoint must be HTTPS. Redirects are not followed. The calling
   application must configure the HTTP manager's proxy and TLS handling
   correctly. The DoH endpoint hostname is passed through that manager; there
   is no separate local lookup inside JDNS.
4. If the DNS transport fails or times out, deliver a matching SERVFAIL to
   JDNS. Do not retry over UDP, system DNS, or a different network path.
5. JDNS accepts decoded records **only** through its existing parser;
   the new transport performs wire framing and lightweight ID matching.

## Downstream Iris/Psi integration plan

1. Land and release this JDNS API independently; run Linux Qt5/6 plus
   Windows/macOS/Android compatibility builds.
2. Replace the custom DNS wire encoder/parser in iris#134. Keep SRV
   priority/weight selection and ordered retries, but obtain DNS answer records
   via QJDns. Scope resolver lifetime to the connection/proxy profile;
   do not mutate the global NameManager resolver.
3. For DoH, Psi constructs and configures a per-proxy QNetworkAccessManager and
   keeps it alive for pending queries. For TCP, Psi passes an explicit
   QNetworkProxy and configured server host/port. Both are supplied to Iris.
4. For the Local choice, keep existing JDNS system resolver. For DoH and TCP,
   do not use local DNS as an implicit fallback. Keep profile persistence and
   DNS test UI from psi#1036.
5. Test with `ssh -D 1337 -N psi` and `spectrum.im`: SRV should yield
   `chat.spectrum.im:5222`, then SOCKS5 CONNECT by hostname; the local
   machine should not issue SRV DNS requests in remote mode.

## Tests to retain

- DNS-over-TCP length-prefixed framing, including split frames.
- DoH POST with RFC 8484 `application/dns-message` body/header.
- Invalid DNS IDs, truncated answers, oversized frames, TCP close and timeout.
- DoH HTTP failures, redirects and TLS errors propagate as DNS errors; no
  implicit network fallback.
- Concurrent queries and request cancellation.
- No multicast/Android regression and no required Qt GUI components.
