/*
 * qjdns_dohtransport.h - DNS-over-HTTPS transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_DOHTRANSPORT_H
#define QJDNS_DOHTRANSPORT_H

#include "qjdns_querytransport.h"
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
class JDNS_EXPORT QJDnsDohTransport : public QJDnsQueryTransport
{
public:
    /**
     * @brief Create a DoH resolver transport with a fixed endpoint.
     * @note All I/O uses the configured endpoint and network policy. The
     * transport is asynchronous, does not perform local DNS fallback and
     * must be handed to QJDns::setQueryTransport() before init().
     */
    QJDnsDohTransport(const QUrl &endpoint, QNetworkAccessManager *manager = nullptr,
                      QObject *parent = nullptr);
    ~QJDnsDohTransport() override;

    /** @brief Return configured QNAM without transferring ownership. */
    QNetworkAccessManager *networkAccessManager() const;
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

#endif // QJDNS_DOHTRANSPORT_H
