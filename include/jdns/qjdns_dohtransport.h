/*
 * qjdns_dohtransport.h - DNS-over-HTTPS transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_DOHTRANSPORT_H
#define QJDNS_DOHTRANSPORT_H

#include "qjdns_transport.h"
#include <QUrl>

class QNetworkAccessManager;

/**
 * RFC 8484 DNS-over-HTTPS using Qt Network.
 *
 * If manager is null, a QNetworkAccessManager is created and owned by this
 * transport. Otherwise, the provided manager remains caller-owned and must
 * outlive active DNS requests. HTTP redirects are not followed.
 *
 * The internally created manager uses Qt's default networking and proxy
 * policy. To guarantee that DoH uses a particular SOCKS5/HTTP proxy, pass
 * an explicitly configured QNetworkAccessManager, or configure the manager
 * returned by networkAccessManager() before starting the first query.
 * No fallback to another DNS transport occurs when HTTPS fails.
 *
 * Composes an internal QJDnsExternalTransport for DNS transaction bookkeeping
 * and timeout without exposing its backend completion methods to callers.
 * For a non-QNAM HTTP/TLS stack (including QCA), use QJDnsExternalTransport
 * directly and perform HTTP requests in the caller.
 */
class JDNS_EXPORT QJDnsDohTransport : public QJDnsTransport
{
public:
    /**
     * @brief Construct an RFC 8484 DNS-over-HTTPS transport.
     * @param endpoint HTTPS DoH URL (for example
     * https://cloudflare-dns.com/dns-query). Invalid, non-HTTPS URLs and URLs
     * containing user information or fragments are rejected when queried.
     * @param manager Optional Qt HTTP manager. If null, this transport creates
     * and owns a QNetworkAccessManager as a QObject child. If non-null, the
     * manager remains caller-owned and must outlive all pending requests.
     * @param parent Optional QObject parent, superseded by successful
     * QJDns::setTransport() ownership transfer.
     * @warning A default-created manager uses Qt's normal proxy configuration:
     * it does NOT inherit an XMPP account's SOCKS5 proxy automatically.
     * Configure networkAccessManager() before starting queries or inject an
     * explicitly configured manager when DNS must not leave the proxy.
     * @note No automatic UDP, local DNS or alternate DoH fallback is attempted
     * on HTTP, TLS or DNS errors.
     */
    QJDnsDohTransport(const QUrl &endpoint, QNetworkAccessManager *manager = nullptr,
                      QObject *parent = nullptr);

    /**
     * @brief Abort active DoH replies and release owned resources.
     * @note The external manager, if provided, is not deleted or reparented.
     */
    ~QJDnsDohTransport() override;

    /**
     * @brief Access the QNAM used for DoH, regardless of ownership.
     * @return Non-owning pointer to the owned or external QNAM, or null if a
     * caller-owned QNAM was destroyed.
     * @warning Do not delete, reparent or take ownership of the returned
     * pointer. Configure proxy/TLS policy before the first request; changing
     * it afterward can change the routing of later queries.
     */
    QNetworkAccessManager *networkAccessManager() const;

    /**
     * @brief Set the per-request HTTP/DNS timeout.
     * @param milliseconds Positive timeout in milliseconds; nonpositive
     * values are ignored. The default is 12000 ms.
     * @note A timeout aborts the HTTP operation, returns SERVFAIL to JDNS,
     * and never retries via the local resolver.
     */
    void setTimeout(int milliseconds);

    /**
     * @brief Advertise managed unicast DNS transport capability.
     * @return ManagedUnicast; multicast and DNS publication are unsupported.
     */
    Capabilities capabilities() const override { return ManagedUnicast; }

    /**
     * @brief Allocate a logical handle for unicast DNS requests.
     * @param address JDNS bind address, not used by the HTTPS backend.
     * @param port JDNS bind port, not used by the HTTPS backend.
     * @param multicastAddress Must be null; multicast is unsupported.
     * @return Nonzero handle on success; 0 for multicast requests.
     */
    int open(const QHostAddress &address, quint16 port,
             const QHostAddress &multicastAddress) override;

    /**
     * @brief Close a DNS handle and cancel pending HTTP operations.
     * @param handle Previously opened logical handle.
     * @note Late replies are ignored. The QNAM itself remains available.
     */
    void close(int handle) override;

    /**
     * @brief Send an asynchronous DoH request.
     * @param handle Open logical transport handle.
     * @param destination JDNS-supplied destination, ignored; the constructor's
     * HTTPS endpoint determines the destination.
     * @param port JDNS destination port, ignored.
     * @param packet Complete DNS wire query without TCP/HTTP framing.
     * @return QJDnsTransport::SubmitResult.
     * @note Sends RFC 8484 POST with application/dns-message for both request
     * and accepted response. Redirects, non-200 status, wrong Content-Type,
     * TLS errors and oversized replies are treated as failures, not fallback.
     */
    SubmitResult submit(int handle, const QHostAddress &destination, quint16 port,
                        const QByteArray &packet) override;

    /**
     * @brief Consume one complete DoH DNS response from the queue.
     * @param handle Open logical transport handle.
     * @param packet Output, containing DNS wire data without HTTP headers.
     * @return true if a response was consumed; false otherwise.
     */
    bool takeResponse(int handle, Packet *packet) override;

private:
    class Private;
    Private *d;
    void submitDns(quint64 id, const QByteArray &packet);
    void cancelDns(quint64 id);
    void finishDns(quint64 id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_DOHTRANSPORT_H
