/*
 * qjdns_externaltransport.h - managed DNS transport with an external backend
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_EXTERNALTRANSPORT_H
#define QJDNS_EXTERNALTRANSPORT_H

#include "qjdns_transport.h"

/**
 * @brief Backend-neutral, asynchronous DNS wire transport.
 *
 * The caller implements ALL networking. It may use QNAM, another HTTP
 * client, a different DoH/DoT library, or an application-owned connection.
 * There is no HTTP, TLS, proxy, or DNS server configuration in this class.
 *
 * QJDns builds the request and parses the response using its existing DNS
 * engine. queryRequested() passes a complete unframed DNS wire message and a
 * unique transport-level request ID. The caller MUST eventually call
 * completeRequest() with the unframed DNS response or failRequest(), unless
 * requestCancelled() was emitted. After a timeout, the transport fails closed
 * by returning SERVFAIL to the DNS engine; it NEVER uses local DNS.
 *
 * Signals are emitted on this QObject's thread. Finish calls must happen on
 * the same thread (use a queued connection when another thread owns the
 * networking implementation). A late result after cancellation is ignored.
 *
 * Ownership: QJDns::setTransport() takes ownership of this object. The caller
 * retains ownership of the external networking backend.
 */
class JDNS_EXPORT QJDnsExternalTransport : public QJDnsTransport
{
    Q_OBJECT
public:
    /**
     * @brief Construct a managed DNS transport with no built-in network I/O.
     * @param parent Optional QObject parent; QJDns::setTransport() assumes
     * ownership on success.
     * @note Connect queryRequested() to an application-owned asynchronous
     * backend before issuing DNS queries. The backend must handle every
     * request it receives; no fallback networking exists in this class.
     */
    explicit QJDnsExternalTransport(QObject *parent = nullptr);

    /**
     * @brief Cancel outstanding requests and destroy the transport.
     * @note Outstanding requests cause requestCancelled() notifications.
     * The application is still responsible for destroying its own backend.
     */
    ~QJDnsExternalTransport() override;

    /**
     * @brief Configure the maximum lifetime of an external DNS request.
     * @param milliseconds Positive timeout in milliseconds; zero/negative
     * values are ignored. Default is 12000 ms.
     * @note Expiry emits requestCancelled() and supplies a DNS SERVFAIL
     * response to JDNS; it never retries via a different DNS transport.
     * New timeout applies to subsequently submitted requests.
     */
    void setTimeout(int milliseconds);

    /**
     * @brief Advertise externally managed unicast DNS transport support.
     * @return ManagedUnicast; multicast queries and publication are rejected.
     */
    Capabilities capabilities() const override { return ManagedUnicast; }

    /**
     * @brief Create a logical DNS transport handle without opening sockets.
     * @param address JDNS bind address (ignored).
     * @param port JDNS bind port (ignored).
     * @param multicastAddress Must be null; multicast is unsupported.
     * @return Nonzero handle on success, 0 when multicast was requested.
     */
    int open(const QHostAddress &address, quint16 port,
             const QHostAddress &multicastAddress) override;

    /**
     * @brief Cancel all pending operations belonging to a logical handle.
     * @param handle Handle created by open().
     * @note Emits requestCancelled() for each outstanding request, releases
     * buffered results, and permanently ignores their late completions.
     */
    void close(int handle) override;

    /**
     * @brief Accept a DNS wire request for asynchronous external delivery.
     * @param handle Open logical handle.
     * @param destination Destination IP supplied by JDNS; ignored by the
     * external transport itself. The application backend decides routing.
     * @param port Destination port supplied by JDNS; likewise ignored.
     * @param packet Complete unframed DNS wire query (header included).
     * @return Submitted for a valid request, Dropped for invalid handle,
     * malformed-size packet or exhausted request ID range.
     * @note packetWritten() and queryRequested() are emitted asynchronously
     * after submit() returns. The backend may call completeRequest() or
     * failRequest() from the queryRequested() handler on this QObject thread.
     * @warning There is no HTTP validation or TLS configuration here; the
     * backend must implement its own network security policy.
     */
    SubmitResult submit(int handle, const QHostAddress &destination,
                        quint16 port, const QByteArray &packet) override;

    /**
     * @brief Consume the next queued DNS response for this handle.
     * @param handle Open logical handle.
     * @param packet Output DNS wire packet when one is available.
     * @return true if a packet was dequeued, false otherwise.
     * @note Both successful DNS replies and synthetic SERVFAIL responses
     * are passed to JDNS for decoding and normal error signaling.
     */
    bool takeResponse(int handle, Packet *packet) override;

public slots:
    /**
     * @brief Complete an external DNS request with its DNS response.
     * @param requestId Unique ID from queryRequested(), not the DNS header ID.
     * @param dnsResponse Complete unframed DNS wire message, including header.
     * @note Validates response length and matching DNS transaction ID; an
     * invalid response is converted to SERVFAIL. The JDNS core still performs
     * actual DNS record decoding. Late/duplicate completions are ignored.
     * @warning Invoke on this object's Qt thread. Use a queued connection
     * when the application backend works on another thread.
     */
    void completeRequest(quint64 requestId, const QByteArray &dnsResponse);

    /**
     * @brief Terminate an external DNS request with a transport error.
     * @param requestId ID from queryRequested().
     * @param reason Diagnostic error text, forwarded through debugLine().
     * @note Removes the pending request and queues synthetic SERVFAIL to
     * JDNS. There is no local DNS or network fallback. Late failures are
     * ignored once the request was already cancelled or completed.
     * @warning Invoke on this object's Qt thread.
     */
    void failRequest(quint64 requestId, const QString &reason);

signals:
    /**
     * @brief Ask the application backend to send one DNS wire query.
     * @param requestId Transport-unique monotonically increasing ID;
     * distinct from the DNS transaction ID within @p dnsRequest.
     * @param dnsRequest Complete unframed DNS request. DoH backends may send
     * it as an application/dns-message POST; other network stacks are allowed.
     * @note Emitted asynchronously on the transport's Qt thread. The backend
     * owns all network resources and must eventually call completeRequest()
     * or failRequest(), unless requestCancelled() was emitted.
     */
    void queryRequested(quint64 requestId, const QByteArray &dnsRequest);

    /**
     * @brief Request cancellation of an in-flight backend operation.
     * @param requestId Request previously delivered by queryRequested().
     * @note Emitted when the logical handle is closed, when the transport is
     * destroyed, or when the per-request timeout expires. Stop/abort the
     * associated network operation. Any later completion for this ID is
     * ignored; there is no additional completion callback required.
     */
    void requestCancelled(quint64 requestId);

private:
    class Private;
    Private *d;
    void retire(quint64 requestId, bool notifyCancellation);
    void queueResponse(int handle, const QByteArray &response);
};

#endif // QJDNS_EXTERNALTRANSPORT_H
