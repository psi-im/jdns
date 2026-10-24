/*
 * qjdns_tcptransport.h - DNS-over-TCP transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_TCPTRANSPORT_H
#define QJDNS_TCPTRANSPORT_H

#include "qjdns_querytransport.h"
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
class JDNS_EXPORT QJDnsTcpTransport : public QJDnsQueryTransport
{
public:
    /**
     * @brief Create a TCP resolver transport with a fixed endpoint.
     * @note All I/O uses the configured endpoint and network policy. The
     * transport is asynchronous, does not perform local DNS fallback and
     * must be handed to QJDns::setQueryTransport() before init().
     */
    QJDnsTcpTransport(const QString &server, quint16 port,
                      const QNetworkProxy &proxy, QObject *parent = nullptr);
    ~QJDnsTcpTransport() override;

    /**
     * @brief Configure per-query timeout, in milliseconds.
     * @note Ignored for nonpositive values; default 12000 ms.
     */
    void setTimeout(int milliseconds);

    /**
     * @brief Begin a DNS wire query without fake socket handles/endpoints.
     * @param id Unique resolver request ID.
     * @param packet Complete DNS wire request, without transport framing.
     */
    void startRequest(int id, const QByteArray &packet) override;

    /**
     * @brief Cancel an unfinished DNS query.
     * @param id Resolver request ID, not the 16-bit DNS packet ID.
     * @note Safe after completion; late replies are ignored.
     */
    void cancelRequest(int id) override;

private:
    class Private;
    Private *d;
    void submitDns(int id, const QByteArray &packet);
    void cancelDns(int id);
    void finishDns(int id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_TCPTRANSPORT_H
