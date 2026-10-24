/*
 * qjdns_tcptransport.h - DNS-over-TCP transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_TCPTRANSPORT_H
#define QJDNS_TCPTRANSPORT_H

#include "qjdns_transport.h"
#include <QNetworkProxy>

/**
 * DNS-over-TCP (RFC 7766) using an explicitly configured Qt socket proxy.
 *
 * The DNS server can be local or remote; the transport name describes the
 * protocol, not its network location. Pass QNetworkProxy::NoProxy explicitly
 * for direct TCP. A SOCKS5 proxy receives the DNS server hostname in CONNECT.
 *
 * Composes an internal QJDnsExternalTransport for request IDs, cancellation,
 * timeouts and returning unframed DNS packets to JDNS. The external backend
 * completion methods are NOT exposed in the TCP transport's public API. No UDP or system-DNS fallback.
 */
class JDNS_EXPORT QJDnsTcpTransport : public QJDnsTransport
{
public:
    /**
     * @brief Configure DNS-over-TCP (RFC 7766) through an explicit proxy.
     * @param server DNS resolver hostname or IP, as seen from the proxy when
     * using SOCKS5. 127.0.0.1 through SSH dynamic forwarding denotes the
     * remote SSH host, not the local application machine.
     * @param port Resolver TCP port, normally 53.
     * @param proxy Explicit QNetworkProxy, including NoProxy for direct TCP.
     * @param parent Optional QObject parent, superseded when QJDns takes
     * ownership via setTransport().
     * @note The server/proxy are fixed for this transport's lifetime; a
     * separate transport is needed for another resolver or proxy policy.
     * @warning No fallback to UDP or system DNS occurs on TCP failure.
     */
    QJDnsTcpTransport(const QString &server, quint16 port,
                      const QNetworkProxy &proxy, QObject *parent = nullptr);

    /** @brief Cancel active TCP operations and release owned resources. */
    ~QJDnsTcpTransport() override;

    /**
     * @brief Set the maximum wait for an individual DNS request.
     * @param milliseconds Positive timeout in milliseconds; nonpositive
     * values are ignored. The default is 12000 ms.
     * @note A timeout cancels the corresponding TCP operation and returns a
     * DNS SERVFAIL response to JDNS. It never triggers a local DNS fallback.
     */
    void setTimeout(int milliseconds);

    /**
     * @brief Advertise that this transport manages unicast requests.
     * @return ManagedUnicast; QJDns::Multicast initialization is unsupported.
     */
    Capabilities capabilities() const override { return ManagedUnicast; }

    /**
     * @brief Allocate a logical unicast handle for JDNS.
     * @param address Requested bind address, unused by this transport.
     * @param port Requested bind port, unused by this transport.
     * @param multicastAddress Must be null; multicast is not supported.
     * @return Nonzero handle on success, or 0 if multicast was requested.
     * @note TCP connections are established per submitted DNS request, not
     * when this logical handle is opened.
     */
    int open(const QHostAddress &address, quint16 port,
             const QHostAddress &multicastAddress) override;

    /**
     * @brief Close a logical handle and cancel its pending DNS requests.
     * @param handle Handle returned by open().
     * @note Late TCP replies cannot complete a closed handle.
     */
    void close(int handle) override;

    /**
     * @brief Start asynchronous transmission of a DNS wire packet over TCP.
     * @param handle Open logical transport handle.
     * @param destination JDNS-provided destination, ignored in favor of
     * the resolver supplied to the constructor.
     * @param port JDNS-provided destination port, likewise ignored.
     * @param packet Complete DNS request, without TCP length prefix.
     * @return QJDnsTransport::SubmitResult.
     * @note This implementation adds RFC 7766 two-byte length framing and
     * does not modify or decode DNS records. It uses the configured Qt proxy.
     */
    SubmitResult submit(int handle, const QHostAddress &destination, quint16 port,
                        const QByteArray &packet) override;

    /**
     * @brief Pop a complete DNS wire response received over TCP.
     * @param handle Logical transport handle.
     * @param packet Output containing the unframed DNS response.
     * @return true if a response was dequeued; false otherwise.
     * @note Successful records are decoded by JDNS, not by this transport.
     */
    bool takeResponse(int handle, Packet *packet) override;

private:
    class Private;
    Private *d;
    void submitDns(quint64 id, const QByteArray &packet);
    void cancelDns(quint64 id);
    void finishDns(quint64 id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_TCPTRANSPORT_H
