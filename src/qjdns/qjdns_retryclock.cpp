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

#include "qjdns_p.h"

#include "qjdns_transport.h"

QJDnsElapsedTimer::QJDnsElapsedTimer(QJDnsTransport *const *transport)
    : transport_(transport)
{
}

void QJDnsElapsedTimer::start()
{
    paused_ = false;
    pauseStartedReal_ = 0;
    lastSampleReal_ = 0;
    lastReturned_ = 0;
    pausedTotal_ = 0;
    timer_.start();
}

qint64 QJDnsElapsedTimer::elapsed() const
{
    const qint64 real = timer_.elapsed();
    const QJDnsTransport *transport = transport_ ? *transport_ : nullptr;
    const bool managedRetryPending = transport
        && transport->capabilities().testFlag(QJDnsTransport::HandlesRetries)
        && transport->hasPendingRequests();

    if(managedRetryPending)
    {
        if(!paused_)
        {
            // jdns sampled the clock immediately before it submitted the
            // packet. Freeze at that previous sample rather than at the
            // first later retry-timer tick, otherwise an 800 ms jdns retry
            // can race the transport which already owns retransmission.
            paused_ = true;
            pauseStartedReal_ = lastSampleReal_;
        }
        lastSampleReal_ = real;
        return lastReturned_;
    }

    if(paused_)
    {
        // The last sample taken while the request was pending is the closest
        // event-loop approximation of transport completion. Do not include
        // the time between consuming the response and this later sample in
        // the paused interval; cache and inactive-query timers must advance.
        pausedTotal_ += lastSampleReal_ - pauseStartedReal_;
        paused_ = false;
    }

    lastSampleReal_ = real;
    lastReturned_ = real - pausedTotal_;
    return lastReturned_;
}
