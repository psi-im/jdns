/*
 * qjdns_dohtransport.cpp - RFC 8484 DNS-over-HTTPS using external QNAM
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#include "qjdns_dohtransport.h"
#include "qjdns_wire_p.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

class QJDnsDohTransport::Private
{
public:
    QUrl endpoint;
    QPointer<QNetworkAccessManager> manager;
    QHash<quint64, QPointer<QNetworkReply>> replies;
};

QJDnsDohTransport::QJDnsDohTransport(const QUrl &endpoint,
                                     QNetworkAccessManager *manager, QObject *parent)
    : QJDnsExternalTransport(parent), d(new Private)
{
    d->endpoint = endpoint;
    d->manager = manager;
    connect(this, &QJDnsExternalTransport::queryRequested, this,
            [this](quint64 id, const QByteArray &packet) { submitDns(id, packet); });
    connect(this, &QJDnsExternalTransport::requestCancelled, this,
            [this](quint64 id) { cancelDns(id); });
}

QJDnsDohTransport::~QJDnsDohTransport()
{
    disconnect(this, nullptr, this, nullptr);
    const auto ids = d->replies.keys();
    for(quint64 id : ids)
        cancelDns(id);
    delete d;
}

void QJDnsDohTransport::cancelDns(quint64 id)
{
    auto reply = d->replies.take(id);
    if(reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void QJDnsDohTransport::finishDns(quint64 id, const QByteArray &response, const QString &error)
{
    if(!d->replies.contains(id))
        return;
    cancelDns(id);
    if(!error.isEmpty())
        failRequest(id, error);
    else
        completeRequest(id, response);
}

void QJDnsDohTransport::submitDns(quint64 id, const QByteArray &packet)
{
    if(!d->manager || !d->endpoint.isValid()
       || d->endpoint.scheme() != QLatin1String("https")
       || d->endpoint.host().isEmpty() || !d->endpoint.userInfo().isEmpty()
       || !d->endpoint.fragment().isEmpty()) {
        failRequest(id, QStringLiteral("DoH requires an HTTPS endpoint and a caller-supplied network manager"));
        return;
    }
    if(packet.size() < 12 || packet.size() > QJDnsWire::MaxMessageSize) {
        failRequest(id, QStringLiteral("Invalid DNS request size"));
        return;
    }

    QNetworkRequest request(d->endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/dns-message"));
    request.setRawHeader("Accept", "application/dns-message");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = d->manager->post(request, packet);
    d->replies.insert(id, reply);
    connect(reply, &QNetworkReply::finished, this, [this, id, reply]() {
        if(!d->replies.contains(id))
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray mime = reply->rawHeader("Content-Type").split(';').value(0).trimmed().toLower();
        if(reply->error() != QNetworkReply::NoError || status != 200
           || mime != QByteArrayLiteral("application/dns-message")) {
            finishDns(id, {}, QStringLiteral("DoH request failed (HTTP %1): %2")
                                 .arg(status).arg(reply->errorString()));
            return;
        }
        if(reply->bytesAvailable() > QJDnsWire::MaxMessageSize) {
            finishDns(id, {}, QStringLiteral("DoH DNS response exceeds size limit"));
            return;
        }
        finishDns(id, reply->readAll(), {});
    });
}
