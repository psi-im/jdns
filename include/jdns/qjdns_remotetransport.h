/*
 * qjdns_remotetransport.h - managed DNS-over-TCP and DoH transport for QJDns
 *
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_REMOTETRANSPORT_H
#define QJDNS_REMOTETRANSPORT_H

#include "qjdns_transport.h"

#include <QNetworkProxy>
#include <QUrl>

class QNetworkAccessManager;

/**
 * @brief Managed unicast DNS transport.
 *
 * QJDns keeps responsibility for generating queries, decoding DNS records,
 * following CNAMEs, and caching. This class only carries complete DNS wire
 * messages over TCP or RFC 8484 DoH.
 *
 * For TCP, the caller supplies an explicit QNetworkProxy and a DNS server.
 * For DoH, the caller supplies a *non-owned* QNetworkAccessManager, configured
 * with the required proxy and TLS policy. No QNetworkAccessManager is ever
 * created implicitly and neither transport falls back to UDP/system DNS.
 *
 * Call configureTcp() or configureDoh() before QJDns::setTransport().
 * Ownership of this object is transferred to QJDns by setTransport().
 * The external QNetworkAccessManager must outlive active DoH operations.
 */
class JDNS_EXPORT QJDnsRemoteTransport : public QJDnsTransport
{
public:
    explicit QJDnsRemoteTransport(QObject *parent = nullptr);
    ~QJDnsRemoteTransport() override;

    void configureTcp(const QString &server, quint16 port, const QNetworkProxy &proxy);
    void configureDoh(const QUrl &endpoint, QNetworkAccessManager *manager);
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
    void complete(quint64 key, const QByteArray &packet);
    void fail(quint64 key, const QString &reason);
    void retire(quint64 key);
};

#endif // QJDNS_REMOTETRANSPORT_H
