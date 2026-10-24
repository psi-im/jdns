/*
 * Shared internal helpers for managed-unicast DNS transports.
 * Copyright (C) 2026 Psi contributors
 * Licensed under the MIT license (see COPYING).
 */
#ifndef QJDNS_WIRE_P_H
#define QJDNS_WIRE_P_H

#include <QByteArray>
#include <QtGlobal>

namespace QJDnsWire {
constexpr int MaxMessageSize = 16384;

inline quint16 read16(const QByteArray &packet, int position)
{
    return (quint16(quint8(packet.at(position))) << 8) | quint8(packet.at(position + 1));
}

// Construct a response to the supplied single-question DNS request. The
// transport uses this only for errors; JDNS still parses all DNS responses.
inline QByteArray servfail(const QByteArray &query)
{
    if(query.size() < 17)
        return {};
    int position = 12;
    bool terminated = false;
    while(position < query.size()) {
        const quint8 label = quint8(query.at(position++));
        if(label == 0) {
            terminated = true;
            break;
        }
        if((label & 0xc0) == 0xc0) {
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
    answer[5] = 1;
    for(int i = 6; i < 12; ++i)
        answer[i] = 0;
    return answer;
}
} // namespace QJDnsWire

#endif
