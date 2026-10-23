/*
 * QJDns transport tests: the SRV wire response is parsed by JDNS itself.
 * Copyright (C) 2026 Psi contributors
 * SPDX-License-Identifier: MIT
 */
#include <qjdns.h>
#include <qjdns_remotetransport.h>

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <cstring>
#include <memory>

namespace {
void put16(QByteArray *buf, quint16 v)
{
    buf->append(char(v >> 8));
    buf->append(char(v));
}
quint16 get16(const QByteArray &buf, int pos)
{
    return quint16(quint8(buf.at(pos)) << 8) | quint8(buf.at(pos + 1));
}
QByteArray encodeName(const QByteArray &name)
{
    QByteArray out;
    for(const QByteArray &part : name.split('.'))
    {
        out.append(char(part.size()));
        out.append(part);
    }
    out.append(char(0));
    return out;
}
QByteArray srvAnswer(const QByteArray &request)
{
    QByteArray result = request;
    result[2] = char(0x81); // QR and RD
    result[3] = char(0x80); // RA and NOERROR
    result[6] = 0;
    result[7] = 1; // one answer
    for(int i = 8; i < 12; ++i)
        result[i] = 0;

    // Compressed owner pointer to original question, followed by SRV rdata.
    result.append(char(0xc0));
    result.append(char(0x0c));
    put16(&result, 33); // SRV
    put16(&result, 1); // IN
    result.append(QByteArray::fromHex("0000003c")); // TTL 60
    const QByteArray target = encodeName("chat.spectrum.im");
    put16(&result, quint16(6 + target.size()));
    put16(&result, 1); // priority
    put16(&result, 1); // weight
    put16(&result, 5222);
    result += target;
    return result;
}
class FakeReply : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, const QByteArray &answer, int status, QObject *parent)
        : QNetworkReply(parent), answer_(answer)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::PostOperation);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/dns-message"));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }
    void abort() override {}
    qint64 bytesAvailable() const override
    {
        return (answer_.size() - position_) + QNetworkReply::bytesAvailable();
    }
protected:
    qint64 readData(char *dst, qint64 maxSize) override
    {
        const qint64 amount = qMin<qint64>(maxSize, answer_.size() - position_);
        if(amount <= 0)
            return -1;
        std::memcpy(dst, answer_.constData() + position_, size_t(amount));
        position_ += amount;
        return amount;
    }
private:
    QByteArray answer_;
    qint64 position_ = 0;
};
class FakeHttp : public QNetworkAccessManager
{
public:
    int calls = 0;
    int status = 200;
    QByteArray lastQuery;
    QUrl lastUrl;
    QByteArray lastAccept;
    QByteArray lastContentType;
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        ++calls;
        lastUrl = request.url();
        lastAccept = request.rawHeader("Accept");
        lastContentType = request.header(QNetworkRequest::ContentTypeHeader).toByteArray();
        lastQuery = outgoingData ? outgoingData->readAll() : QByteArray();
        if(operation != PostOperation)
            return new FakeReply(request, {}, 405, this);
        return new FakeReply(request, srvAnswer(lastQuery), status, this);
    }
};

bool containsExpectedSrv(const QJDns::Response &response)
{
    for(const auto &record : response.answerRecords)
    {
        if(record.type == QJDns::Srv && record.port == 5222
           && record.name.contains("chat.spectrum.im"))
            return true;
    }
    return false;
}
} // namespace

class RemoteTransportTest : public QObject
{
    Q_OBJECT
private slots:
    void tcpFramingAndSrvParsing()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *client = server.nextPendingConnection();
            auto *received = new QByteArray;
            connect(client, &QTcpSocket::readyRead, client, [client, received, &requests]() {
                *received += client->readAll();
                if(received->size() < 2)
                    return;
                const int frameSize = get16(*received, 0);
                if(received->size() < frameSize + 2)
                    return;
                const QByteArray answer = srvAnswer(received->mid(2, frameSize));
                QByteArray framed;
                put16(&framed, quint16(answer.size()));
                framed += answer;
                ++requests;
                client->write(framed.left(3)); // exercise fragmented TCP framing
                QTimer::singleShot(10, client, [client, framed]() { client->write(framed.mid(3)); });
            });
            connect(client, &QObject::destroyed, &server, [received]() { delete received; });
        });

        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        transport->configureTcp(QStringLiteral("127.0.0.1"), server.serverPort(),
                                QNetworkProxy(QNetworkProxy::NoProxy));
        QVERIFY(dns.setTransport(transport));
        QVERIFY(dns.init(QJDns::Unicast, QHostAddress::AnyIPv4));
        bool success = false;
        bool failure = false;
        connect(&dns, &QJDns::resultsReady, &dns, [&](int, const QJDns::Response &response) {
            success = containsExpectedSrv(response);
        });
        connect(&dns, &QJDns::error, &dns, [&](int, QJDns::Error) { failure = true; });
        QVERIFY(dns.queryStart("_xmpp-client._tcp.spectrum.im.", QJDns::Srv) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(success || failure, 5000);
        QVERIFY(success);
        QVERIFY(!failure);
        QCOMPARE(requests, 1);
    }

    void tcpOverSocks5ResolvesDnsServerRemotely()
    {
        QTcpServer socks;
        QVERIFY(socks.listen(QHostAddress::LocalHost, 0));
        QString connectHost;
        int responses = 0;
        connect(&socks, &QTcpServer::newConnection, &socks, [&]() {
            auto *client = socks.nextPendingConnection();
            struct Session {
                enum State { Greeting, Connect, Data } state = Greeting;
                QByteArray buffer;
            };
            auto session = std::make_shared<Session>();
            connect(client, &QTcpSocket::readyRead, client, [&, client, session]() {
                session->buffer += client->readAll();
                for(;;)
                {
                    if(session->state == Session::Greeting)
                    {
                        if(session->buffer.size() < 2)
                            return;
                        const int methods = quint8(session->buffer.at(1));
                        if(session->buffer.size() < methods + 2)
                            return;
                        session->buffer.remove(0, methods + 2);
                        client->write(QByteArray::fromHex("0500")); // SOCKS5, no authentication
                        session->state = Session::Connect;
                    }
                    else if(session->state == Session::Connect)
                    {
                        if(session->buffer.size() < 5)
                            return;
                        if(quint8(session->buffer.at(0)) != 5 ||
                           quint8(session->buffer.at(1)) != 1 ||
                           quint8(session->buffer.at(3)) != 3)
                            return;
                        const int length = quint8(session->buffer.at(4));
                        if(session->buffer.size() < length + 7)
                            return;
                        connectHost = QString::fromLatin1(session->buffer.mid(5, length));
                        session->buffer.remove(0, length + 7);
                        client->write(QByteArray::fromHex("050000017f0000010035"));
                        session->state = Session::Data;
                    }
                    else
                    {
                        if(session->buffer.size() < 2)
                            return;
                        const int length = get16(session->buffer, 0);
                        if(session->buffer.size() < length + 2)
                            return;
                        const QByteArray answer = srvAnswer(session->buffer.mid(2, length));
                        QByteArray framed;
                        put16(&framed, quint16(answer.size()));
                        framed += answer;
                        session->buffer.remove(0, length + 2);
                        ++responses;
                        client->write(framed);
                        return;
                    }
                }
            });
        });

        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        transport->configureTcp(QStringLiteral("dns-server.invalid"), 53,
                                QNetworkProxy(QNetworkProxy::Socks5Proxy,
                                              QStringLiteral("127.0.0.1"), socks.serverPort()));
        QVERIFY(dns.setTransport(transport));
        QVERIFY(dns.init(QJDns::Unicast, QHostAddress::AnyIPv4));
        bool success = false;
        connect(&dns, &QJDns::resultsReady, &dns, [&](int, const QJDns::Response &answer) {
            success = containsExpectedSrv(answer);
        });
        QVERIFY(dns.queryStart("_xmpp-client._tcp.spectrum.im.", QJDns::Srv) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(success, 5000);
        QCOMPARE(connectHost, QStringLiteral("dns-server.invalid"));
        QCOMPARE(responses, 1);
    }

    void dohUsesCallerManagerAndJdnsParser()
    {
        FakeHttp manager;
        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        const QUrl url(QStringLiteral("https://example.test/dns-query"));
        transport->configureDoh(url, &manager);
        QVERIFY(dns.setTransport(transport));
        QVERIFY(dns.init(QJDns::Unicast, QHostAddress::AnyIPv4));
        bool success = false;
        connect(&dns, &QJDns::resultsReady, &dns, [&](int, const QJDns::Response &response) {
            success = containsExpectedSrv(response);
        });
        QVERIFY(dns.queryStart("_xmpp-client._tcp.spectrum.im.", QJDns::Srv) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(success, 5000);
        QCOMPARE(manager.calls, 1);
        QCOMPARE(manager.lastUrl, url);
        QCOMPARE(manager.lastAccept, QByteArray("application/dns-message"));
        QCOMPARE(manager.lastContentType, QByteArray("application/dns-message"));
        QVERIFY(manager.lastQuery.size() >= 12);
    }

    void dohHttpFailureNeverFallsBack()
    {
        FakeHttp manager;
        manager.status = 503;
        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        transport->configureDoh(QUrl(QStringLiteral("https://example.test/dns-query")), &manager);
        QVERIFY(dns.setTransport(transport));
        QVERIFY(dns.init(QJDns::Unicast, QHostAddress::AnyIPv4));
        bool failed = false;
        connect(&dns, &QJDns::error, &dns, [&](int, QJDns::Error) { failed = true; });
        QVERIFY(dns.queryStart("_xmpp-client._tcp.spectrum.im.", QJDns::Srv) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(failed, 5000);
        QCOMPARE(manager.calls, 1);
    }

    void tcpFailureNeverFallsBack()
    {
        QTcpServer temp;
        QVERIFY(temp.listen(QHostAddress::LocalHost, 0));
        const quint16 closedPort = temp.serverPort();
        temp.close();

        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        transport->configureTcp(QStringLiteral("127.0.0.1"), closedPort, QNetworkProxy(QNetworkProxy::NoProxy));
        QVERIFY(dns.setTransport(transport));
        QVERIFY(dns.init(QJDns::Unicast, QHostAddress::AnyIPv4));
        bool failed = false;
        connect(&dns, &QJDns::error, &dns, [&](int, QJDns::Error) { failed = true; });
        QVERIFY(dns.queryStart("_xmpp-client._tcp.spectrum.im.", QJDns::Srv) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(failed, 5000);
    }

    void customManagedTransportCannotPublishMulticast()
    {
        QJDns dns;
        auto *transport = new QJDnsRemoteTransport;
        QVERIFY(dns.setTransport(transport));
        QVERIFY(!dns.init(QJDns::Multicast, QHostAddress::AnyIPv4));
    }
};
QTEST_MAIN(RemoteTransportTest)
#include "remotetransport.moc"
