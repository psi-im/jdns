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
    QJDnsDohTransport(const QUrl &endpoint, QNetworkAccessManager *manager = nullptr,
                      QObject *parent = nullptr);
    ~QJDnsDohTransport() override;

    // Non-owning accessor. The transport owns this manager only when it
    // created the default one; callers may configure proxy/TLS before queries.
    QNetworkAccessManager *networkAccessManager() const;
    void setTimeout(int milliseconds);
    Capabilities capabilities() const override { return ManagedUnicast; }
    int open(const QHostAddress &address, quint16 port, const QHostAddress &multicastAddress) override;
    void close(int handle) override;
    SubmitResult submit(int handle, const QHostAddress &destination, quint16 port,
                        const QByteArray &packet) override;
    bool takeResponse(int handle, Packet *packet) override;

private:
    class Private;
    Private *d;
    void submitDns(quint64 id, const QByteArray &packet);
    void cancelDns(quint64 id);
    void finishDns(quint64 id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_DOHTRANSPORT_H
