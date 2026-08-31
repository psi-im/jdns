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

#include "qjdns_transport.h"

#include "qjdns_sock.h"

#include <QHash>
#include <QUdpSocket>

#ifdef Q_OS_ANDROID
QJDnsTransport *qjdns_create_android_transport(QObject *parent);
#endif

class QJDnsSocketTransport : public QJDnsTransport
{
public:
    explicit QJDnsSocketTransport(QObject *parent) : QJDnsTransport(parent) {}

    int open(const QHostAddress &address, quint16 port, const QHostAddress &multicastAddress) override
    {
        auto *socket = new QUdpSocket(this);

        QUdpSocket::BindMode bindMode;
        bindMode |= QUdpSocket::ShareAddress;
        bindMode |= QUdpSocket::ReuseAddressHint;
        if(!socket->bind(address, port, bindMode))
        {
            delete socket;
            return 0;
        }

        if(!multicastAddress.isNull())
        {
            const int sd = socket->socketDescriptor();
            bool ok;
            int errorCode;
            if(multicastAddress.protocol() == QAbstractSocket::IPv6Protocol)
                ok = qjdns_sock_setMulticast6(sd, multicastAddress.toIPv6Address().c, &errorCode);
            else
                ok = qjdns_sock_setMulticast4(sd, multicastAddress.toIPv4Address(), &errorCode);

            if(!ok)
            {
                emit debugLine(QString("failed to setup multicast on the socket (errorCode=%1)").arg(errorCode));
                delete socket;
                return 0;
            }

            if(multicastAddress.protocol() == QAbstractSocket::IPv6Protocol)
            {
                qjdns_sock_setTTL6(sd, 255);
                qjdns_sock_setIPv6Only(sd);
            }
            else
                qjdns_sock_setTTL4(sd, 255);
        }

        const int handle = nextHandle_++;
        sockets_.insert(handle, socket);

        connect(socket, &QUdpSocket::readyRead, this, [this, handle]() { emit readyRead(handle); });
        connect(socket, &QUdpSocket::bytesWritten, this, [this](qint64) { emit packetWritten(); }, Qt::QueuedConnection);
        return handle;
    }

    void close(int handle) override
    {
        auto *socket = sockets_.take(handle);
        delete socket;
    }

    SubmitResult submit(int handle, const QHostAddress &destination, quint16 port, const QByteArray &packet) override
    {
        auto *socket = sockets_.value(handle);
        if(!socket)
            return RetryLater;

        if(socket->writeDatagram(packet, destination, port) == -1)
        {
            // Preserve the historical QJDns behaviour: a datagram that cannot
            // be written is treated as dropped rather than retried forever.
            return Dropped;
        }
        return Submitted;
    }

    bool takeResponse(int handle, Packet *packet) override
    {
        auto *socket = sockets_.value(handle);
        if(!socket || !socket->hasPendingDatagrams())
            return false;

        const qint64 pendingSize = socket->pendingDatagramSize();
        if(pendingSize < 0)
            return false;

        QByteArray data;
        data.resize(static_cast<int>(pendingSize));
        QHostAddress sourceAddress;
        quint16 sourcePort = 0;
        const qint64 size = socket->readDatagram(data.data(), data.size(), &sourceAddress, &sourcePort);
        if(size < 0)
            return false;

        data.resize(static_cast<int>(size));
        packet->sourceAddress = sourceAddress;
        packet->sourcePort = sourcePort;
        packet->data = data;
        return true;
    }

private:
    int nextHandle_ = 1;
    QHash<int, QUdpSocket *> sockets_;
};

QJDnsTransport *qjdns_create_transport(QObject *parent, bool unicast)
{
#ifdef Q_OS_ANDROID
    if(unicast)
    {
        if(QJDnsTransport *transport = qjdns_create_android_transport(parent))
            return transport;
    }
#else
    Q_UNUSED(unicast)
#endif
    return new QJDnsSocketTransport(parent);
}
