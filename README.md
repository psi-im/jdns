### JDNS

Date: October 1st, 2005
Author: Justin Karneges <justin@affinix.com>

JDNS is a simple DNS implementation that can perform normal DNS queries
of any record type (notably SRV), as well as Multicast DNS queries and
advertising.  Multicast support is based on Jeremie Miller's "mdnsd"
implementation.

For maximum flexibility, JDNS is written in C with no direct dependencies,
and is licensed under the MIT license.  Your application must supply
functionality to JDNS, such as UDP sending/receiving, via callbacks.

For Qt users there is a wrapper available called QJDns.  jdns.pri can
be used to include everything into a qmake project. CMake-project will build
the sample Qt-based commandline tool 'jdns'.

#### Features:
* DNS client "stub" resolver
* Can fetch any record type, but provides handy decoding for many
    known types: A, AAAA, SRV, MX, TXT, etc.
* Performs retries, caching/expiration, and CNAME following
* Algorithm logic adapted from Q3Dns
* Multicast queries
* Multicast advertising

#### Why?
* Trolltech is phasing out the Qt DNS implementation, which in Qt 4 has
  been relegated to the Qt3Support module.  A replacement was desired.

* While there are many DNS libraries available, at the time of this
  writing it was (and still may be) hard to find one that satisfies
  three essential conditions: cross-platform friendliness (and this
  includes Windows 9x!), the ability to integrate into existing
  eventloops, sensible licensing (ie, not GPL).

#### How to use:
* Prepare callbacks and call jdns_session_new()
* Call jdns_init_unicast() or jdns_init_multicast(), depending on
  if you want regular or multicast DNS.  If you want both kinds, you
  can always make two sessions.
* Make queries and have fun
* Call jdns_step() at the right times to advance JDNS processing

#### What is left to you:
* The callback functions, obviously.
* Querying for several "qualified" names.  Here is what Q3Dns does:
    Query for name as provided
    Query for name + '.domain' (for every domain the computer is in)
* Detecting for '.local' in a name to be queried, and using that
  to decide whether to query via Multicast or normal DNS.
* Recognition of IP addresses.  If you want an IP address to resolve
  to itself, then do that yourself.  Passing an IP address as a DNS
  name to JDNS won't work (especially since it wouldn't make any
  sense in some contexts, like SRV).
* Recognition of known hosts.  If you want this, compare inputs against
  jdns_system_dnsparams().
* For zeroconf/Bonjour, keep in mind that JDNS only provides Multicast
  DNS capability.  DNS-SD and any higher layers would be your job.

Using a custom DNS implementation has the drawback that it is difficult
to take advantage of platform-specific features (for example, an OS-wide
DNS cache or LDAP integration).

An application strategy for normal DNS should probably be:
* If an A or AAAA record is desired, use a native lookup.
* Else, if the platform has advanced DNS features already (ie,
  res_query), use those.
* Else, use JDNS.

However, it may not be a bad idea at first to use JDNS for all occasions,
so that it can be debugged.

For Multicast DNS, awareness of the platform is doubly important.  There
should only be one Multicast DNS "Responder" per computer, and using JDNS
at the same time could result in a conflict.

An application strategy for Multicast DNS should be:
* If the platform has a Multicast DNS daemon installed already, use
  it somehow.
* Else, use JDNS.

Have fun!

