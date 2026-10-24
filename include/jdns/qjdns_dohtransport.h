/*
 * qjdns_dohtransport.h - DNS-over-HTTPS transport for QJDns
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_DOHTRANSPORT_H
#define QJDNS_DOHTRANSPORT_H

#include "qjdns_externaltransport.h"
#include <QUrl>

class QNetworkAccessManager;

/**
 * RFC 8484 DNS-over-HTTPS over a caller-owned QNetworkAccessManager.
 *
 * The caller MUST configure the manager's HTTPS, proxy and certificate
 * policy before starting any queries and keep it alive while DNS is active.
 * No hidden QNAM is created. HTTP redirects are not followed.
 *
 * Uses QJDnsExternalTransport for DNS transaction bookkeeping and timeout.
 * For a non-QNAM HTTP/TLS stack (including QCA), use QJDnsExternalTransport
 * directly and perform HTTP requests in the caller.
 */
class JDNS_EXPORT QJDnsDohTransport : public QJDnsExternalTransport
{
public:
    QJDnsDohTransport(const QUrl &endpoint, QNetworkAccessManager *manager,
                      QObject *parent = nullptr);
    ~QJDnsDohTransport() override;

private:
    class Private;
    Private *d;
    void submitDns(quint64 id, const QByteArray &packet);
    void cancelDns(quint64 id);
    void finishDns(quint64 id, const QByteArray &response, const QString &error);
};

#endif // QJDNS_DOHTRANSPORT_H
