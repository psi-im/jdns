/*
 * qjdns_externaltransport.h - application-owned asynchronous DNS backend
 * SPDX-License-Identifier: MIT
 */
#ifndef QJDNS_EXTERNALTRANSPORT_H
#define QJDNS_EXTERNALTRANSPORT_H

#include "qjdns_querytransport.h"

class JDNS_EXPORT QJDnsExternalTransport : public QJDnsQueryTransport
{
    Q_OBJECT
public:
    /**
     * @brief Create an HTTP-, TLS- and proxy-agnostic DNS transport.
     *
     * Applications connect queryRequested() to any asynchronous network
     * backend, then invoke completeRequest()/failRequest(). The backend and
     * its TLS, endpoint and proxy policy remain wholly application-owned.
     */
    explicit QJDnsExternalTransport(QObject *parent = nullptr);
    ~QJDnsExternalTransport() override;

    /**
     * @brief Set timeout for subsequently initiated requests.
     * @param milliseconds Positive number of milliseconds; default 12000.
     * @note Timeout emits requestCancelled() and failed(..., Timeout).
     */
    void setTimeout(int milliseconds);

    void startRequest(int id, const QByteArray &dnsPacket) override;
    void cancelRequest(int id) override;

public slots:
    /**
     * @brief Return an unframed DNS response for a pending request.
     * @param id Resolver request ID from queryRequested().
     * @param dnsResponse Complete DNS wire message.
     * @note Late and duplicate results are ignored. Call on the QObject
     * thread; for other threads use a queued invocation.
     * JDNS validates the DNS transaction ID and question.
     */
    void completeRequest(int id, const QByteArray &dnsResponse);

    /**
     * @brief Report failure of an application-owned network operation.
     * @param id Resolver request ID from queryRequested().
     * @param reason Diagnostic string; no DNS query is retried locally.
     * @param kind NetworkError or Timeout.
     */
    void failRequest(int id, const QString &reason, Failure kind = NetworkError);

signals:
    /**
     * @brief Ask external code to perform asynchronous network delivery.
     * @param id Logical resolver request ID, not DNS transaction ID.
     * @param dnsRequest Complete unframed DNS query.
     * @note Emitted on next event-loop turn, never within startRequest().
     */
    void queryRequested(int id, const QByteArray &dnsRequest);

    /**
     * @brief Cancel a previously requested external operation.
     * @param id ID supplied to queryRequested().
     * @note Emitted for timeout, caller cancellation and destruction.
     */
    void requestCancelled(int id);

private:
    struct Private;
    Private *d;
    void forget(int id, bool notifyCancel);
};

#endif // QJDNS_EXTERNALTRANSPORT_H
