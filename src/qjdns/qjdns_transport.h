/*
 * Copyright (C) 2026  Psi contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef QJDNS_TRANSPORT_H
#define QJDNS_TRANSPORT_H

#include <QByteArray>
#include <QFlags>
#include <QHostAddress>
#include <QObject>

class QJDnsTransport : public QObject
{
    Q_OBJECT
public:
    enum Capability {
        HandlesRetries = 0x01,
        HandlesFailover = 0x02,
        UsesSystemNameServers = 0x04
    };
    Q_DECLARE_FLAGS(Capabilities, Capability)

    enum SubmitResult {
        RetryLater,
        Dropped,
        Submitted
    };

    struct Packet
    {
        QHostAddress sourceAddress;
        quint16 sourcePort = 0;
        QByteArray data;
    };

    explicit QJDnsTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~QJDnsTransport() override = default;

    virtual Capabilities capabilities() const { return {}; }
    virtual bool hasPendingRequests() const { return false; }
    virtual int open(const QHostAddress &address, quint16 port, const QHostAddress &multicastAddress) = 0;
    virtual void close(int handle) = 0;
    virtual SubmitResult submit(int handle, const QHostAddress &destination, quint16 port, const QByteArray &packet) = 0;
    virtual bool takeResponse(int handle, Packet *packet) = 0;

signals:
    void readyRead(int handle);
    void packetWritten();
    void debugLine(const QString &line);
};

Q_DECLARE_OPERATORS_FOR_FLAGS(QJDnsTransport::Capabilities)

QJDnsTransport *qjdns_create_transport(QObject *parent, bool unicast);

#endif
