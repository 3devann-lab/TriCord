#ifndef NETWORK_DNS_H
#define NETWORK_DNS_H

#include <string>

namespace Network {
namespace Dns {

// When on, hosts are looked up through Google DNS instead of the network's resolver, which gets
// around networks that block Discord by its DNS names. TLS still checks the real host name.
void setEnabled(bool enabled);
bool isEnabled();

// The IPv4 address for host, from Google DNS-over-HTTPS (dns.google, pinned to 8.8.8.8) or plain DNS to
// 8.8.8.8/8.8.4.4. Cached; empty when every lookup fails. Blocks, so call it off the UI thread.
std::string resolve(const std::string &host);

// "host" out of "https://host:port/path".
std::string hostOf(const std::string &url);

} // namespace Dns
} // namespace Network

#endif // NETWORK_DNS_H
