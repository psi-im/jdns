/*
 * qjdns_remotetransport.cpp - managed DNS-over-TCP and RFC 8484 DoH
 *
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#include "qjdns_remotetransport.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QQueue>
#include <QSet>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int MaxDnsMessageSize = 16384; // matches JDNS_UNICAST_IN_MAX
constexpr int DefaultTimeoutMs = 12000;

quint16 read16(const QByteArray &packet, int position)
{
    return (quint16(quint8(packet.at(position))) << 8) | quint8(packet.at(position + 1));
}

void append16(QByteArray *packet, quint16 value)
{
    packet->append(char(value >> 8));
    packet->append(char(value & 0xff));
}

quint64 transactionKey(int handle, quint16 id)
{
    return (quint64(quint32(handle)) << 16) | id;
}

// Only builds a minimal error response for a query produced by JDNS.
// Successful DNS responses are parsed exclusively by the existing JDNS core.
QByteArray serverFailure(const QByteArray &query)
{
    if(query.size() < 17)
        return {};
    int position = 12;
    bool terminated = false;
    while(position < query.size())
    {
        const quint8 label = quint8(query.at(position++));
        if(label == 0)
        {
            terminated = true;
            break;
        }
        if((label & 0xc0) == 0xc0)
        {
            if(position >= query.size())
                return {};
            ++position;
            terminated = true;
            break;
        }
        if((label & 0xc0) || position + label > query.size())
            return {};
        position += label;
    }
    if(!terminated || position + 4 > query.size())
        return {};

    QByteArray answer = query.left(position + 4);
    const quint16 flags = quint16(0x8000 | 0x0080 | (read16(query, 2) & 0x0100) | 2);
    answer[2] = char(flags >> 8);
    answer[3] = char(flags & 0xff);
    answer[4] = 0;
    answer[5] = 1; // exactly one question
    for(int i = 6; i < 12; ++i)
        answer[i] = 0;
    return answer;
}
}

class QJDnsRemoteTransport::Private
{
public:
    enum Mode { Unconfigured, Tcp, Doh };
    struct Request
    {
        int handle = 0;
        QByteArray query;
        QPointer<QTcpSocket> socket;
        QPointer<QNetworkReply> reply;
        QTimer *timer = nullptr;
        QByteArray received;
        int length = -1;
    };

    Mode mode = Unconfigured;
    QString server;
    quint16 serverPort = 0;
    QNetworkProxy proxy;
    QUrl endpoint;
    QPointer<QNetworkAccessManager> manager;
    int timeoutMs = DefaultTimeoutMs;

    int nextHandle = 1;
    QSet<int> handles;
    QHash<quint64, Request *> active;
    QHash<int, QQueue<QByteArray>> responses;
};

QJDnsRemoteTransport::QJDnsRemoteTransport(QObject *parent)
    : QJDnsTransport(parent), d(new Private)
{
}

QJDnsRemoteTransport::~QJDnsRemoteTransport()
{
    const auto keys = d->active.keys();
    for(quint64 key : keys)
        retire(key);
    delete d;
}

void QJDnsRemoteTransport::configureTcp(const QString &server, quint16 port, const QNetworkProxy &proxy)
{
    if(!d->handles.isEmpty())
        return;
    d->mode = Private::Tcp;
    d->server = server;
    d->serverPort = port;
    d->proxy = proxy;
}

void QJDnsRemoteTransport::configureDoh(const QUrl &endpoint, QNetworkAccessManager *manager)
{
    if(!d->handles.isEmpty())
        return;
    d->mode = Private::Doh;
    d->endpoint = endpoint;
    d->manager = manager;
}

void QJDnsRemoteTransport::setTimeout(int milliseconds)
{
    if(milliseconds > 0)
        d->timeoutMs = milliseconds;
}

int QJDnsRemoteTransport::open(const QHostAddress &, quint16, const QHostAddress &multicastAddress)
{
    if(!multicastAddress.isNull())
        return 0;
    const int handle = d->nextHandle++;
    d->handles.insert(handle);
    return handle;
}

void QJDnsRemoteTransport::retire(quint64 key)
{
    Private::Request *request = d->active.take(key);
    if(!request)
        return;
    if(request->timer)
    {
        request->timer->stop();
        delete request->timer;
    }
    if(request->socket)
    {
        request->socket->disconnect(this);
        request->socket->abort();
        request->socket->deleteLater();
    }
    if(request->reply)
    {
        request->reply->disconnect(this);
        request->reply->abort();
        request->reply->deleteLater();
    }
    delete request;
}

void QJDnsRemoteTransport::close(int handle)
{
    if(!d->handles.remove(handle))
        return;
    const auto keys = d->active.keys();
    for(quint64 key : keys)
    {
        Private::Request *request = d->active.value(key);
        if(request && request->handle == handle)
            retire(key);
    }
    d->responses.remove(handle);
}

void QJDnsRemoteTransport::complete(quint64 key, const QByteArray &response)
{
    Private::Request *request = d->active.value(key);
    if(!request)
        return;
    const int handle = request->handle;
    const QByteArray query = request->query;
    retire(key);

    if(!d->handles.contains(handle))
        return;

    // The DNS transaction ID must match. JDNS validates and decodes content.
    QByteArray answer = response;
    if(answer.size() < 12 || answer.size() > MaxDnsMessageSize || read16(answer, 0) != read16(query, 0))
    {
        emit debugLine(QStringLiteral("Rejected invalid DNS response from remote transport"));
        answer = serverFailure(query);
    }
    if(answer.isEmpty())
        return;
    d->responses[handle].enqueue(answer);
    QTimer::singleShot(0, this, [this, handle]() {
        if(d->handles.contains(handle) && !d->responses.value(handle).isEmpty())
            emit readyRead(handle);
    });
}

void QJDnsRemoteTransport::fail(quint64 key, const QString &reason)
{
    auto *request = d->active.value(key);
    if(!request)
        return;
    emit debugLine(reason);
    complete(key, serverFailure(request->query));
}

QJDnsTransport::SubmitResult QJDnsRemoteTransport::submit(int handle, const QHostAddress &, quint16,
                                                           const QByteArray &packet)
{
    if(!d->handles.contains(handle) || packet.size() < 12 || packet.size() > MaxDnsMessageSize)
        return Dropped;

    const quint64 key = transactionKey(handle, read16(packet, 0));
    if(d->active.contains(key))
        return Dropped;

    auto *request = new Private::Request;
    request->handle = handle;
    request->query = packet;
    request->timer = new QTimer(this);
    request->timer->setSingleShot(true);
    d->active.insert(key, request);
    connect(request->timer, &QTimer::timeout, this, [this, key]() {
        fail(key, QStringLiteral("Remote DNS request timed out"));
    });
    request->timer->start(d->timeoutMs);

    if(d->mode == Private::Tcp && !d->server.isEmpty() && d->serverPort != 0)
    {
        auto *socket = new QTcpSocket(this);
        request->socket = socket;
        socket->setProxy(d->proxy);
        connect(socket, &QTcpSocket::connected, this, [this, key, socket]() {
            if(!d->active.contains(key))
                return;
            QByteArray framed;
            append16(&framed, quint16(d->active.value(key)->query.size()));
            framed += d->active.value(key)->query;
            if(socket->write(framed) != framed.size())
                fail(key, QStringLiteral("Unable to send DNS-over-TCP request"));
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, key, socket]() {
            auto *r = d->active.value(key);
            if(!r)
                return;
            r->received += socket->readAll();
            if(r->length == -1 && r->received.size() >= 2)
            {
                r->length = read16(r->received, 0);
                if(r->length < 12 || r->length > MaxDnsMessageSize)
                {
                    fail(key, QStringLiteral("Invalid DNS-over-TCP frame length"));
                    return;
                }
            }
            if(r->length >= 0 && r->received.size() >= r->length + 2)
                complete(key, r->received.mid(2, r->length));
        });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        connect(socket, &QTcpSocket::errorOccurred, this, [this, key, socket](QAbstractSocket::SocketError) {
            fail(key, QStringLiteral("DNS-over-TCP socket: %1").arg(socket->errorString()));
        });
#else
        connect(socket, static_cast<void (QAbstractSocket::*)(QAbstractSocket::SocketError)>(&QAbstractSocket::error),
                this, [this, key, socket](QAbstractSocket::SocketError) {
                    fail(key, QStringLiteral("DNS-over-TCP socket: %1").arg(socket->errorString()));
                });
#endif
        connect(socket, &QTcpSocket::disconnected, this, [this, key]() {
            fail(key, QStringLiteral("DNS-over-TCP closed before a complete response"));
        });
        socket->connectToHost(d->server, d->serverPort);
    }
    else if(d->mode == Private::Doh && d->manager
            && d->endpoint.isValid() && d->endpoint.scheme() == QLatin1String("https")
            && !d->endpoint.host().isEmpty() && d->endpoint.userInfo().isEmpty())
    {
        QNetworkRequest netRequest(d->endpoint);
        netRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/dns-message"));
        netRequest.setRawHeader("Accept", "application/dns-message");
        netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply *reply = d->manager->post(netRequest, packet);
        request->reply = reply;
        connect(reply, &QNetworkReply::finished, this, [this, key, reply]() {
            if(!d->active.contains(key))
                return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray mime = reply->rawHeader("Content-Type").split(';').value(0).trimmed().toLower();
            if(reply->error() != QNetworkReply::NoError || status != 200
               || mime != QByteArrayLiteral("application/dns-message"))
            {
                fail(key, QStringLiteral("DoH request failed (HTTP %1): %2")
                              .arg(status).arg(reply->errorString()));
                return;
            }
            complete(key, reply->readAll());
        });
    }
    else
    {
        QTimer::singleShot(0, this, [this, key]() {
            fail(key, QStringLiteral("Remote DNS transport is not configured"));
        });
    }

    // JDNS tracks outstanding writes during shutdown. Every accepted query
    // must acknowledge its submission, independent of its eventual outcome.
    QTimer::singleShot(0, this, [this]() { emit packetWritten(); });
    return Submitted;
}

bool QJDnsRemoteTransport::takeResponse(int handle, Packet *packet)
{
    auto it = d->responses.find(handle);
    if(!packet || it == d->responses.end() || it->isEmpty())
        return false;
    packet->data = it->dequeue();
    packet->sourceAddress = QHostAddress();
    packet->sourcePort = 0;
    if(it->isEmpty())
        d->responses.erase(it);
    return true;
}
