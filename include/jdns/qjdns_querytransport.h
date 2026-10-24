/*
 * qjdns_querytransport.h - asynchronous unicast DNS query transport
 * SPDX-License-Identifier: MIT
 */
#ifndef QJDNS_QUERYTRANSPORT_H
#define QJDNS_QUERYTRANSPORT_H

#include "jdns_export.h"

#include <QByteArray>
#include <QObject>

/**
 * @brief Execute complete DNS wire requests without socket-shaped APIs.
 *
 * The resolver encodes requests, validates and decodes responses, coalesces
 * consumers, follows CNAME records and caches TTLs. Transports own network
 * endpoint selection, retry and timeout policy. Unicast transport operations
 * never bind a listener or handle multicast.
 *
 * @note QJDns takes ownership on successful setQueryTransport() and uses this
 * interface on its QObject thread. Completion must not be emitted reentrantly
 * from startRequest(): defer even immediate errors to the event loop.
 * @warning Do not choose an implicit alternative DNS path after an error.
 */
class JDNS_EXPORT QJDnsQueryTransport : public QObject
{
    Q_OBJECT
public:
    enum Failure {
        NetworkError, ///< Socket, HTTP, TLS or protocol error; no DNS fallback.
        Timeout       ///< Transport timed out.
    };
    Q_ENUM(Failure)

    explicit QJDnsQueryTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~QJDnsQueryTransport() override = default;

    /**
     * @brief Start one complete asynchronous DNS operation.
     * @param id Unique in-flight resolver request ID (not DNS header ID).
     * @param dnsPacket Complete unframed DNS wire query.
     * @note A request must produce exactly one responseReady() or failed()
     * unless the resolver calls cancelRequest().
     */
    virtual void startRequest(int id, const QByteArray &dnsPacket) = 0;

    /**
     * @brief Abort an unfinished DNS operation.
     * @param id ID supplied to startRequest().
     * @note Cancel must be safe for an unknown or already-completed ID.
     * Do not emit completion after cancelling.
     */
    virtual void cancelRequest(int id) = 0;

signals:
    /**
     * @brief Deliver complete unframed DNS response data to the resolver.
     * @param id ID supplied to startRequest().
     * @param dnsPacket DNS reply, not TCP framing or HTTP body headers.
     */
    void responseReady(int id, const QByteArray &dnsPacket);

    /**
     * @brief Report network or protocol failure without fabricating SERVFAIL.
     * @param id ID supplied to startRequest().
     * @param reason Network/TLS/HTTP diagnostic information.
     * @param kind Distinguishes timed-out from generic failures.
     */
    void failed(int id, const QString &reason, QJDnsQueryTransport::Failure kind);

    /** @brief Emit debug diagnostics to the resolver. */
    void debugLine(const QString &line);
};

#endif // QJDNS_QUERYTRANSPORT_H
