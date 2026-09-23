#pragma once
#include <cstddef>
#include <cstdint>
// Bounded, single-question captive DNS response. IPv4 address is in network order.
std::size_t captiveDnsReply(uint8_t *packet, std::size_t length, std::size_t capacity, uint32_t ip);
void captiveDnsSetEnabled(bool enabled);
