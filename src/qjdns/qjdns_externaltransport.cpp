/*
 * qjdns_externaltransport.cpp - backend-neutral asynchronous DNS transport
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#include "qjdns_externaltransport.h"
#include "qjdns_wire_p.h"

#include <QHash>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <QTimer>

class QJDnsExternalTransport::Private
{
public:
    struct Request {
        int handle = 0;
        QByteArray query;
        QTimer *timer = nullptr;
    };

    int nextHandle = 1;
    quint64 nextRequestId = 1;
    int timeoutMs = 12000;
    QSet<int> handles;
    QHash<quint64, Request *> outstanding;
    QHash<int, QQueue<QByteArray>> responses;
};

QJDnsExternalTransport::QJDnsExternalTransport(QObject *parent)
    : QJDnsTransport(parent), d(new Private)
{
}

QJDnsExternalTransport::~QJDnsExternalTransport()
{
    const auto ids = d->outstanding.keys();
    for(quint64 id : ids)
        retire(id, true);
    delete d;
}

void QJDnsExternalTransport::setTimeout(int milliseconds)
{
    if(milliseconds > 0)
        d->timeoutMs = milliseconds;
}

int QJDnsExternalTransport::open(const QHostAddress &, quint16, const QHostAddress &multicastAddress)
{
    if(!multicastAddress.isNull())
        return 0;

    const int handle = d->nextHandle++;
    d->handles.insert(handle);
    return handle;
}

void QJDnsExternalTransport::retire(quint64 requestId, bool notifyCancellation)
{
    auto *request = d->outstanding.take(requestId);
    if(!request)
        return;
    request->timer->stop();
    // retire() can run inside the timer's own timeout signal.
    request->timer->deleteLater();
    delete request;

    // Remove the transaction before emitting cancellation, so a completion
    // arriving synchronously from the backend cannot revive a cancelled query.
    if(notifyCancellation)
        emit requestCancelled(requestId);
}

void QJDnsExternalTransport::close(int handle)
{
    if(!d->handles.remove(handle))
        return;

    // Per-query request IDs are independent from DNS transaction IDs.
    // This avoids accepting a stale completion after a handle is closed.
    const auto ids = d->outstanding.keys();
    for(quint64 id : ids) {
        auto *request = d->outstanding.value(id);
        if(request && request->handle == handle)
            retire(id, true);
    }
    d->responses.remove(handle);
}

QJDnsTransport::SubmitResult QJDnsExternalTransport::submit(
    int handle, const QHostAddress &, quint16, const QByteArray &packet)
{
    if(!d->handles.contains(handle) || packet.size() < 12
       || packet.size() > QJDnsWire::MaxMessageSize)
        return Dropped;
    if(d->nextRequestId == 0)
        return Dropped; // Do not reuse request IDs after uint64 overflow.

    const quint64 id = d->nextRequestId++;
    auto *request = new Private::Request;
    request->handle = handle;
    request->query = packet;
    request->timer = new QTimer(this);
    request->timer->setSingleShot(true);
    d->outstanding.insert(id, request);
    connect(request->timer, &QTimer::timeout, this, [this, id]() {
        auto *r = d->outstanding.value(id);
        if(!r)
            return;
        const QByteArray original = r->query;
        const int handle = r->handle;
        retire(id, true);
        emit debugLine(QStringLiteral("External DNS request timed out"));
        queueResponse(handle, QJDnsWire::servfail(original));
    });
    request->timer->start(d->timeoutMs);

    // JDNS invokes submit() within jdns_step(). Do not let application code
    // call back into the resolver before that operation has returned.
    QTimer::singleShot(0, this, [this, id]() {
        emit packetWritten();
        const auto *r = d->outstanding.value(id);
        if(r)
            emit queryRequested(id, r->query);
    });
    return Submitted;
}

void QJDnsExternalTransport::queueResponse(int handle, const QByteArray &response)
{
    if(response.isEmpty() || !d->handles.contains(handle))
        return;
    d->responses[handle].enqueue(response);
    QTimer::singleShot(0, this, [this, handle]() {
        if(d->handles.contains(handle) && !d->responses.value(handle).isEmpty())
            emit readyRead(handle);
    });
}

void QJDnsExternalTransport::completeRequest(quint64 requestId, const QByteArray &dnsResponse)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *r = d->outstanding.value(requestId);
    if(!r)
        return; // cancelled or already completed

    const QByteArray original = r->query;
    const int handle = r->handle;
    retire(requestId, false);

    if(dnsResponse.size() < 12 || dnsResponse.size() > QJDnsWire::MaxMessageSize
       || QJDnsWire::read16(dnsResponse, 0) != QJDnsWire::read16(original, 0)) {
        emit debugLine(QStringLiteral("External DNS backend returned invalid response or transaction ID"));
        queueResponse(handle, QJDnsWire::servfail(original));
        return;
    }
    queueResponse(handle, dnsResponse);
}

void QJDnsExternalTransport::failRequest(quint64 requestId, const QString &reason)
{
    Q_ASSERT(thread() == QThread::currentThread());
    auto *r = d->outstanding.value(requestId);
    if(!r)
        return;

    const QByteArray original = r->query;
    const int handle = r->handle;
    retire(requestId, false);
    emit debugLine(QStringLiteral("External DNS request failed: %1").arg(reason));
    queueResponse(handle, QJDnsWire::servfail(original));
}

bool QJDnsExternalTransport::takeResponse(int handle, Packet *packet)
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
