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

#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)

#include <QCoreApplication>
#include <QHash>
#include <QJniObject>
#include <QQueue>
#include <QSet>
#include <QSocketNotifier>

#include <android/multinetwork.h>
#include <dlfcn.h>

#include <cstdint>
#include <cstring>

namespace {

class AndroidResolverApi
{
public:
    using ResNsend = int (*)(net_handle_t, const uint8_t *, size_t, uint32_t);
    using ResNresult = int (*)(int, int *, uint8_t *, size_t);
    using ResCancel = void (*)(int);

    AndroidResolverApi()
    {
        library_ = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if(!library_)
            return;

        nsend = load<ResNsend>("android_res_nsend");
        nresult = load<ResNresult>("android_res_nresult");
        cancel = load<ResCancel>("android_res_cancel");
    }

    ~AndroidResolverApi()
    {
        if(library_)
            dlclose(library_);
    }

    bool available() const
    {
        return nsend && nresult && cancel;
    }

    ResNsend nsend = nullptr;
    ResNresult nresult = nullptr;
    ResCancel cancel = nullptr;

private:
    template<typename T>
    T load(const char *name) const
    {
        void *symbol = dlsym(library_, name);
        T function = nullptr;
        static_assert(sizeof(function) == sizeof(symbol), "function and data pointers must have the same size");
        std::memcpy(&function, &symbol, sizeof(function));
        return function;
    }

    void *library_ = nullptr;
};

AndroidResolverApi &resolverApi()
{
    static AndroidResolverApi api;
    return api;
}

net_handle_t activeNetworkHandle()
{
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if(!context.isValid())
        return 0;

    const QJniObject serviceName = QJniObject::fromString(QStringLiteral("connectivity"));
    const QJniObject connectivityManager = context.callObjectMethod(
        "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", serviceName.object<jstring>());
    if(!connectivityManager.isValid())
        return 0;

    const QJniObject network = connectivityManager.callObjectMethod(
        "getActiveNetwork", "()Landroid/net/Network;");
    if(!network.isValid())
        return 0;

    const jlong handle = network.callMethod<jlong>("getNetworkHandle", "()J");
    return static_cast<net_handle_t>(handle);
}

quint16 dnsId(const QByteArray &packet)
{
    if(packet.size() < 2)
        return 0;
    return (static_cast<quint16>(static_cast<quint8>(packet[0])) << 8)
        | static_cast<quint8>(packet[1]);
}

QByteArray makeServfail(const QByteArray &query)
{
    if(query.size() < 12)
        return QByteArray();

    int pos = 12;
    bool questionComplete = false;
    while(pos < query.size())
    {
        const quint8 labelLength = static_cast<quint8>(query[pos++]);
        if(labelLength == 0)
        {
            questionComplete = true;
            break;
        }
        if((labelLength & 0xc0) == 0xc0)
        {
            if(pos >= query.size())
                return QByteArray();
            ++pos;
            questionComplete = true;
            break;
        }
        if((labelLength & 0xc0) != 0 || pos + labelLength > query.size())
            return QByteArray();
        pos += labelLength;
    }

    if(!questionComplete || pos + 4 > query.size())
        return QByteArray();

    QByteArray response = query.left(pos + 4);
    const quint16 queryFlags = (static_cast<quint16>(static_cast<quint8>(query[2])) << 8)
        | static_cast<quint8>(query[3]);
    const quint16 responseFlags = static_cast<quint16>(0x8000 | 0x0080 | 0x0002 | (queryFlags & 0x0100));
    response[2] = static_cast<char>(responseFlags >> 8);
    response[3] = static_cast<char>(responseFlags & 0xff);

    // No answer, authority, or additional records.
    for(int i = 6; i < 12; ++i)
        response[i] = 0;

    return response;
}

class QJDnsAndroidResolverTransport : public QJDnsTransport
{
public:
    explicit QJDnsAndroidResolverTransport(QObject *parent)
        : QJDnsTransport(parent)
    {
    }

    ~QJDnsAndroidResolverTransport() override
    {
        const QList<int> fds = transactions_.keys();
        for(int fd : fds)
            cancelTransaction(fd);
    }

    Capabilities capabilities() const override
    {
        return HandlesRetries | HandlesFailover | UsesSystemNameServers;
    }

    int open(const QHostAddress &, quint16, const QHostAddress &multicastAddress) override
    {
        if(!multicastAddress.isNull())
            return 0;

        const int handle = nextHandle_++;
        handles_.insert(handle);
        return handle;
    }

    void close(int handle) override
    {
        handles_.remove(handle);

        const QList<int> fds = transactions_.keys();
        for(int fd : fds)
        {
            const Transaction *transaction = transactions_.value(fd);
            if(transaction && transaction->handle == handle)
                cancelTransaction(fd);
        }

        auto queueIt = responses_.find(handle);
        if(queueIt != responses_.end())
        {
            while(!queueIt->isEmpty())
                outstanding_.remove(queueIt->dequeue().key);
            responses_.erase(queueIt);
        }
    }

    SubmitResult submit(int handle, const QHostAddress &, quint16, const QByteArray &packet) override
    {
        if(!handles_.contains(handle))
            return RetryLater;
        if(packet.size() < 12)
            return Dropped;

        const quint16 id = dnsId(packet);
        const quint64 key = transactionKey(handle, id);

        // jdns may revisit its own retry timer before it processes the
        // response. Android's resolver already owns retries and failover,
        // so never submit the same DNS transaction twice.
        if(outstanding_.contains(key))
            return Dropped;

        outstanding_.insert(key);

        const net_handle_t network = activeNetworkHandle();
        if(network == 0)
        {
            queueFailure(handle, key, packet, QStringLiteral("Android has no active network"));
            queuePacketWritten();
            return Submitted;
        }

        AndroidResolverApi &api = resolverApi();
        const int fd = api.nsend(network,
                                 reinterpret_cast<const uint8_t *>(packet.constData()),
                                 static_cast<size_t>(packet.size()), 0);
        if(fd < 0)
        {
            queueFailure(handle, key, packet,
                         QStringLiteral("android_res_nsend failed (%1)").arg(fd));
            queuePacketWritten();
            return Submitted;
        }

        auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        auto *transaction = new Transaction;
        transaction->handle = handle;
        transaction->key = key;
        transaction->query = packet;
        transaction->notifier = notifier;
        transactions_.insert(fd, transaction);

        connect(notifier, &QSocketNotifier::activated, this,
                [this, fd](QSocketDescriptor, QSocketNotifier::Type) {
                    completeTransaction(fd);
                });

        queuePacketWritten();
        return Submitted;
    }

    bool takeResponse(int handle, Packet *packet) override
    {
        auto it = responses_.find(handle);
        if(it == responses_.end() || it->isEmpty())
            return false;

        QueuedResponse response = it->dequeue();
        if(it->isEmpty())
            responses_.erase(it);

        outstanding_.remove(response.key);
        *packet = response.packet;
        return true;
    }

private:
    struct Transaction
    {
        int handle = 0;
        quint64 key = 0;
        QByteArray query;
        QSocketNotifier *notifier = nullptr;
    };

    struct QueuedResponse
    {
        quint64 key = 0;
        Packet packet;
    };

    static quint64 transactionKey(int handle, quint16 id)
    {
        return (static_cast<quint64>(static_cast<quint32>(handle)) << 16) | id;
    }

    void queuePacketWritten()
    {
        QMetaObject::invokeMethod(this, [this]() { emit packetWritten(); }, Qt::QueuedConnection);
    }

    void queueReadyRead(int handle)
    {
        QMetaObject::invokeMethod(this, [this, handle]() {
            if(handles_.contains(handle) && responses_.contains(handle) && !responses_[handle].isEmpty())
                emit readyRead(handle);
        }, Qt::QueuedConnection);
    }

    void queueResponse(int handle, quint64 key, const QByteArray &data)
    {
        QueuedResponse response;
        response.key = key;
        response.packet.sourceAddress = QHostAddress::LocalHost;
        response.packet.sourcePort = 53;
        response.packet.data = data;
        responses_[handle].enqueue(response);
        queueReadyRead(handle);
    }

    void queueFailure(int handle, quint64 key, const QByteArray &query, const QString &reason)
    {
        emit debugLine(reason);
        const QByteArray response = makeServfail(query);
        if(response.isEmpty())
        {
            outstanding_.remove(key);
            return;
        }
        queueResponse(handle, key, response);
    }

    void completeTransaction(int fd)
    {
        Transaction *transaction = transactions_.take(fd);
        if(!transaction)
            return;

        if(transaction->notifier)
        {
            transaction->notifier->setEnabled(false);
            delete transaction->notifier;
            transaction->notifier = nullptr;
        }

        QByteArray answer(65536, '\0');
        int rcode = 0;
        const int size = resolverApi().nresult(
            fd, &rcode, reinterpret_cast<uint8_t *>(answer.data()), static_cast<size_t>(answer.size()));

        if(size < 0)
        {
            queueFailure(transaction->handle, transaction->key, transaction->query,
                         QStringLiteral("android_res_nresult failed (%1)").arg(size));
        }
        else
        {
            answer.resize(size);
            queueResponse(transaction->handle, transaction->key, answer);
        }

        delete transaction;
    }

    void cancelTransaction(int fd)
    {
        Transaction *transaction = transactions_.take(fd);
        if(!transaction)
            return;

        if(transaction->notifier)
        {
            transaction->notifier->setEnabled(false);
            delete transaction->notifier;
        }

        resolverApi().cancel(fd);
        outstanding_.remove(transaction->key);
        delete transaction;
    }

    int nextHandle_ = 1;
    QSet<int> handles_;
    QSet<quint64> outstanding_;
    QHash<int, Transaction *> transactions_;
    QHash<int, QQueue<QueuedResponse>> responses_;
};

} // namespace

QJDnsTransport *qjdns_create_android_transport(QObject *parent)
{
    if(QNativeInterface::QAndroidApplication::sdkVersion() < 29)
        return nullptr;
    if(!resolverApi().available())
        return nullptr;
    return new QJDnsAndroidResolverTransport(parent);
}

#else

QJDnsTransport *qjdns_create_android_transport(QObject *)
{
    return nullptr;
}

#endif
