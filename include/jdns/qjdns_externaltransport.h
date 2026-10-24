/*
 * qjdns_externaltransport.h - managed DNS transport with an external backend
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_EXTERNALTRANSPORT_H
#define QJDNS_EXTERNALTRANSPORT_H

#include "qjdns_transport.h"

/**
 * @brief Backend-neutral, asynchronous DNS wire transport.
 *
 * The caller implements ALL networking. It may use QNAM, a QCA-based HTTP
 * client, a different DoH/DoT library, or an application-owned connection.
 * There is no HTTP, TLS, proxy, or DNS server configuration in this class.
 *
 * QJDns builds the request and parses the response using its existing DNS
 * engine. queryRequested() passes a complete unframed DNS wire message and a
 * unique transport-level request ID. The caller MUST eventually call
 * completeRequest() with the unframed DNS response or failRequest(), unless
 * requestCancelled() was emitted. After a timeout, the transport fails closed
 * by returning SERVFAIL to the DNS engine; it NEVER uses local DNS.
 *
 * Signals are emitted on this QObject's thread. Finish calls must happen on
 * the same thread (use a queued connection when another thread owns the
 * networking implementation). A late result after cancellation is ignored.
 *
 * Ownership: QJDns::setTransport() takes ownership of this object. The caller
 * retains ownership of the external networking backend.
 */
class JDNS_EXPORT QJDnsExternalTransport : public QJDnsTransport
{
    Q_OBJECT
public:
    explicit QJDnsExternalTransport(QObject *parent = nullptr);
    ~QJDnsExternalTransport() override;

    void setTimeout(int milliseconds);

    Capabilities capabilities() const override { return ManagedUnicast; }
    int open(const QHostAddress &address, quint16 port, const QHostAddress &multicastAddress) override;
    void close(int handle) override;
    SubmitResult submit(int handle, const QHostAddress &destination, quint16 port,
                        const QByteArray &packet) override;
    bool takeResponse(int handle, Packet *packet) override;

public slots:
    void completeRequest(quint64 requestId, const QByteArray &dnsResponse);
    void failRequest(quint64 requestId, const QString &reason);

signals:
    void queryRequested(quint64 requestId, const QByteArray &dnsRequest);
    // The backend should abort its in-flight operation and ignore later replies.
    void requestCancelled(quint64 requestId);

private:
    class Private;
    Private *d;
    void retire(quint64 requestId, bool notifyCancellation);
    void queueResponse(int handle, const QByteArray &response);
};

#endif // QJDNS_EXTERNALTRANSPORT_H
