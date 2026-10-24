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
 * RFC 8484 DNS-over-HTTPS over a caller-owned QNetworkAccessManager.
 *
 * The caller MUST configure the manager's HTTPS, proxy and certificate
 * policy before starting any queries and keep it alive while DNS is active.
 * No hidden QNAM is created. HTTP redirects are not followed.
 *
 * Composes an internal QJDnsExternalTransport for DNS transaction bookkeeping
 * and timeout without exposing its backend completion methods to callers.
 * For a non-QNAM HTTP/TLS stack (including QCA), use QJDnsExternalTransport
 * directly and perform HTTP requests in the caller.
 */
class JDNS_EXPORT QJDnsDohTransport : public QJDnsTransport
{
public:
    QJDnsDohTransport(const QUrl &endpoint, QNetworkAccessManager *manager,
                      QObject *parent = nullptr);
    ~QJDnsDohTransport() override;

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
