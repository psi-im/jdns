/*
 * qjdns_tcptransport.h - DNS-over-TCP transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_TCPTRANSPORT_H
#define QJDNS_TCPTRANSPORT_H

#include "qjdns_externaltransport.h"
#include <QNetworkProxy>

/**
 * DNS-over-TCP (RFC 7766) using an explicitly configured Qt socket proxy.
 *
 * The DNS server can be local or remote; the transport name describes the
 * protocol, not its network location. Pass QNetworkProxy::NoProxy explicitly
 * for direct TCP. A SOCKS5 proxy receives the DNS server hostname in CONNECT.
 *
 * Uses QJDnsExternalTransport for request IDs, cancellation, timeouts and
 * returning unframed DNS packets to JDNS. No UDP or system-DNS fallback.
 */
class JDNS_EXPORT QJDnsTcpTransport : public QJDnsExternalTransport
{
public:
    QJDnsTcpTransport(const QString &server, quint16 port,
                      const QNetworkProxy &proxy, QObject *parent = nullptr);
    ~QJDnsTcpTransport() override;

private:
    class Private;
    Private *d;
    void submitDns(quint64 id, const QByteArray &packet);
    void cancelDns(quint64 id);
    void finishDns(quint64 id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_TCPTRANSPORT_H
