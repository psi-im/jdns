/*
 * qjdns_dohtransport.cpp - RFC 8484 DNS-over-HTTPS using external QNAM
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#include "qjdns_dohtransport.h"
#include "qjdns_externaltransport.h"
#include "qjdns_wire_p.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

class QJDnsDohTransport::Private
{
public:
    QJDnsExternalTransport *core = nullptr;
    QUrl endpoint;
    QPointer<QNetworkAccessManager> manager;
    QHash<quint64, QPointer<QNetworkReply>> replies;
};

QJDnsDohTransport::QJDnsDohTransport(const QUrl &endpoint,
                                     QNetworkAccessManager *manager, QObject *parent)
    : QJDnsTransport(parent), d(new Private)
{
    d->endpoint = endpoint;
    d->manager = manager;
    d->core = new QJDnsExternalTransport(this);
    connect(d->core, &QJDnsExternalTransport::queryRequested, this,
            [this](quint64 id, const QByteArray &packet) { submitDns(id, packet); });
    connect(d->core, &QJDnsExternalTransport::requestCancelled, this,
            [this](quint64 id) { cancelDns(id); });
    connect(d->core, &QJDnsTransport::packetWritten, this, &QJDnsTransport::packetWritten);
    connect(d->core, &QJDnsTransport::readyRead, this, &QJDnsTransport::readyRead);
    connect(d->core, &QJDnsTransport::debugLine, this, &QJDnsTransport::debugLine);
}

QJDnsDohTransport::~QJDnsDohTransport()
{
    d->core->disconnect(this);
    const auto ids = d->replies.keys();
    for(quint64 id : ids)
        cancelDns(id);
    delete d;
}

void QJDnsDohTransport::setTimeout(int milliseconds)
{
    d->core->setTimeout(milliseconds);
}

int QJDnsDohTransport::open(const QHostAddress &address, quint16 port, const QHostAddress &multicastAddress)
{
    return d->core->open(address, port, multicastAddress);
}

void QJDnsDohTransport::close(int handle)
{
    d->core->close(handle);
}

QJDnsTransport::SubmitResult QJDnsDohTransport::submit(int handle, const QHostAddress &destination,
                                                        quint16 port, const QByteArray &packet)
{
    return d->core->submit(handle, destination, port, packet);
}

bool QJDnsDohTransport::takeResponse(int handle, Packet *packet)
{
    return d->core->takeResponse(handle, packet);
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
        d->core->failRequest(id, error);
    else
        d->core->completeRequest(id, response);
}

void QJDnsDohTransport::submitDns(quint64 id, const QByteArray &packet)
{
    if(!d->manager || !d->endpoint.isValid()
       || d->endpoint.scheme() != QLatin1String("https")
       || d->endpoint.host().isEmpty() || !d->endpoint.userInfo().isEmpty()
       || !d->endpoint.fragment().isEmpty()) {
        d->core->failRequest(id, QStringLiteral("DoH requires an HTTPS endpoint and a caller-supplied network manager"));
        return;
    }
    if(packet.size() < 12 || packet.size() > QJDnsWire::MaxMessageSize) {
        d->core->failRequest(id, QStringLiteral("Invalid DNS request size"));
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
