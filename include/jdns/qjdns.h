/*
 * Copyright (C) 2005,2006  Justin Karneges
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

/**
 * @file
 *
 * The Qt wrapper to jdns.
 */

#ifndef QJDNS_H
#define QJDNS_H

#include "jdns_export.h"
#include <QtCore>
#include <QtNetwork>

class QJDnsTransport;
class QJDnsQueryTransport;

/**
 * @brief Low-level, asynchronous Qt wrapper around the JDNS DNS engine.
 *
 * A QJDns instance handles one unicast or multicast DNS session. To use a
 * custom DNS-over-TCP, DoH or external backend, call setTransport() exactly
 * once before init(). Otherwise platform-default UDP transport is used.
 *
 * JDNS still encodes/decodes DNS messages; transport injection changes only
 * the network I/O and the semantics of DNS server selection. The caller owns
 * QJDns, while QJDns takes ownership of a successfully injected transport.
 *
 * @note QJDns is QObject/event-loop based. Keep the instance and its
 * transport on the same Qt thread.
 * @warning Selecting a managed transport does not automatically configure a
 * proxy: explicit TCP/HTTP transport settings determine where packets go.
 * @see QJDnsTransport, QJDnsTcpTransport, QJDnsDohTransport,
 * QJDnsExternalTransport
 */
class JDNS_EXPORT QJDns : public QObject
{
    Q_OBJECT
public:
    /** @brief DNS session mode; managed transports support Unicast only. */
    enum Mode
    {
        Unicast,
        Multicast
    };

    /** @brief Ownership semantics when publishing multicast DNS records. */
    enum PublishMode
    {
        Unique,
        Shared
    };

    /** @brief Common DNS RR type codes as defined by the DNS protocol. */
    enum Type
    {
        A       = 1,
        Aaaa    = 28,
        Mx      = 15,
        Srv     = 33,
        Cname   = 5,
        Ptr     = 12,
        Txt     = 16,
        Hinfo   = 13,
        Ns      = 2,
        Any     = 255
    };

    /** @brief Errors emitted for DNS queries or multicast publication. */
    enum Error
    {
        ErrorGeneric,
        ErrorNXDomain, ///< query only
        ErrorTimeout,  ///< query only
        ErrorConflict  ///< publish only
    };

    class JDNS_EXPORT NameServer
    {
    public:
        QHostAddress address;
        int port;

        /** @brief Initialize a nameserver endpoint with default port 53. */
        NameServer();
    };

    class JDNS_EXPORT DnsHost
    {
    public:
        QByteArray name;
        QHostAddress address;
    };

    class JDNS_EXPORT SystemInfo
    {
    public:
        QList<NameServer> nameServers;
        QList<QByteArray> domains;
        QList<DnsHost> hosts;
    };

    class JDNS_EXPORT Record
    {
    public:
        QByteArray owner;
        int ttl;
        int type;
        QByteArray rdata;
        bool haveKnown;

        // known
        QHostAddress address;    ///< for A, Aaaa
        QByteArray name;         ///< for Mx, Srv, Cname, Ptr, Ns
        int priority;            ///< for Mx, Srv
        int weight;              ///< for Srv
        int port;                ///< for Srv
        QList<QByteArray> texts; ///< for Txt
        QByteArray cpu;          ///< for Hinfo
        QByteArray os;           ///< for Hinfo

        /** @brief Initialize an empty DNS resource record. */
        Record();

        /**
         * @brief Check whether this record can be encoded for publication.
         * @return true when its type/data satisfy JDNS record validation.
         */
        bool verify() const;
    };

    class JDNS_EXPORT Response
    {
    public:
        QList<Record> answerRecords;
        QList<Record> authorityRecords;
        QList<Record> additionalRecords;
    };

    /**
     * @brief Construct a DNS session.
     * @param parent Optional QObject owner.
     */
    QJDns(QObject *parent = 0);

    /** @brief Destroy the DNS session and its owned transport. */
    ~QJDns();

    /**
     * @brief Inject the sole network transport for this DNS instance.
     * @param transport Heap-allocated QObject transport; must be non-null.
     * @return true if accepted; false if already initialized, if a transport
     * was already configured, or if @p transport is null.
     * @note Ownership transfers to QJDns **only when true** is returned.
     * If false, the caller remains responsible for the transport. On success,
     * it is reparented to QJDns's private implementation and deleted during
     * shutdown/cleanup; do not reuse it for another DNS instance.
     * @warning Call before init(). ManagedUnicast transports are incompatible
     * with Multicast mode. If no transport is injected, default network I/O
     * is provided by the platform (normally UDP).
     */
    bool setTransport(QJDnsTransport *transport);

    /**
     * @brief Initialize the DNS engine and chosen network transport.
     * @param mode Unicast resolution or multicast querying/publishing.
     * @param address Network interface/bind address used by JDNS. Managed
     * unicast transports may ignore this address and open logical handles.
     * @return true when initialization succeeds; false otherwise.
     * @note init() may delete a configured transport if initialization fails;
     * do not retain or dereference a non-owning pointer to it afterward.
     * ManagedUnicast transports are rejected when @p mode is Multicast.
     */
    bool init(Mode mode, const QHostAddress &address);

    /**
     * @brief Start orderly asynchronous shutdown of an initialized session.
     * @note Handles and pending requests are retired by the DNS engine.
     * Wait for shutdownFinished() when the caller needs clean termination.
     * Does not select or fall back to another resolver.
     */
    void shutdown();

    /**
     * @brief Drain accumulated diagnostic messages.
     * @return Collected messages; reading clears the current backlog.
     * @see debugLinesReady()
     */
    QStringList debugLines();

    /**
     * @brief Read host OS DNS configuration.
     * @return Configured resolver addresses, search domains and hosts.
     * @warning This consults local system DNS *configuration*. It is not
     * required for proxied managed transport use; callers seeking to avoid
     * local DNS leaks should not use it to select a fallback resolver.
     */
    static SystemInfo systemInfo();

    /**
     * @brief Detect a primary network interface suitable for multicast.
     * @param address IPv4/IPv6 address family or desired interface.
     * @return Interface address, or a null QHostAddress if detection fails.
     * @note This method probes multicast networking; it is unrelated to
     * the new managed unicast DNS transports.
     */
    static QHostAddress detectPrimaryMulticast(const QHostAddress &address);

    /**
     * @brief Set explicit resolver addresses for normal unicast DNS.
     * @param list DNS servers that ordinary JDNS unicast packets may target.
     * @note Ignored for ManagedUnicast transports: TCP/DoH/external routing
     * must be configured on those transports themselves.
     */
    void setNameServers(const QList<NameServer> &list);

    /**
     * @brief Start an asynchronous DNS resource record lookup.
     * @param name DNS name to query; use wire-compatible encoding.
     * @param type DNS record type, such as QJDns::Srv or QJDns::A.
     * @return JDNS request identifier; use it for resultsReady(), error(),
     * and queryCancel(). A nonpositive value indicates failure to start.
     * @note The query uses only this instance's selected transport. For a
     * managed transport, query completion and timeouts originate from that
     * transport; no implicit system-DNS retry is performed.
     */
    int queryStart(const QByteArray &name, int type);

    /**
     * @brief Cancel a previously started query.
     * @param id Identifier returned by queryStart().
     * @note JDNS stops delivering results for the cancelled query, but may
     * retain the underlying DNS transaction briefly for cache population.
     * Therefore queryCancel() does not guarantee that a previously submitted
     * TCP/HTTPS operation is aborted immediately. Closing the transport
     * handle or shutting down the session cancels associated network I/O.
     */
    void queryCancel(int id);

    /**
     * @brief Begin publishing a DNS resource record via multicast DNS.
     * @param m Unique or Shared publication semantics.
     * @param record DNS resource record to publish.
     * @return Publication handle used for updates, cancellation and signals.
     * @warning Multicast mode only; not supported by any ManagedUnicast
     * transport such as TCP, DoH or External.
     */
    int publishStart(PublishMode m, const Record &record);

    /**
     * @brief Replace the value of an active multicast publication.
     * @param id Publication handle from publishStart().
     * @param record Replacement resource record.
     * @note Has no meaning for ordinary unicast DNS resolution.
     */
    void publishUpdate(int id, const Record &record);

    /**
     * @brief Stop publishing a multicast DNS record.
     * @param id Publication handle from publishStart().
     */
    void publishCancel(int id);

signals:
    /**
     * @brief A DNS query has returned resource records.
     * @param id Identifier returned by queryStart().
     * @param results Answer, authority and additional record sections.
     * @note Filter answerRecords by the desired DNS type: CNAMEs and other
     * related records may accompany the requested RR type.
     */
    void resultsReady(int id, const QJDns::Response &results);

    /**
     * @brief A multicast publication has been acknowledged.
     * @param id Publication handle from publishStart().
     */
    void published(int id);

    /**
     * @brief A query or multicast publication failed.
     * @param id Operation handle from queryStart()/publishStart().
     * @param e DNS error category; consult debugLines() for diagnostics.
     * @note Managed transports report network failures as DNS error
     * responses instead of transparently choosing a local DNS server.
     */
    void error(int id, QJDns::Error e);

    /** @brief Orderly asynchronous shutdown is complete. */
    void shutdownFinished();

    /** @brief New messages are available via debugLines(). */
    void debugLinesReady();

private:
    class Private;
    friend class Private;
    Private *d;
};

#endif
