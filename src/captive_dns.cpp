// Based on Espressif's CC0 captive_portal example, with bounded question parsing.
#include "captive_dns.h"
#include <atomic>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

std::size_t captiveDnsReply(uint8_t *p, std::size_t n, std::size_t cap, uint32_t ip)
{
    if (n < 17 || n > cap || (p[2] & 0xF8) || p[4] || p[5] != 1) return 0;
    std::size_t pos = 12;
    while (pos < n && p[pos]) {
        unsigned len = p[pos++];
        if (len > 63 || pos + len >= n) return 0;
        pos += len;
    }
    if (pos + 5 > n) return 0;
    pos++;
    bool a = p[pos] == 0 && p[pos + 1] == 1 && p[pos + 2] == 0 && p[pos + 3] == 1;
    pos += 4;
    p[2] = 0x81; p[3] = 0x80;
    std::memset(p + 6, 0, 6);
    if (!a) return pos;
    if (pos + 16 > cap) return 0;
    p[7] = 1;
    const uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4};
    std::memcpy(p + pos, answer, sizeof(answer));
    std::memcpy(p + pos + sizeof(answer), &ip, 4);
    return pos + 16;
}

namespace {
    std::atomic<bool> enabled{false};
    TaskHandle_t task;
    void run(void *) {
        for (;;) {
            if (!enabled.load()) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
            int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (fd < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            sockaddr_in addr{};
            addr.sin_family = AF_INET; addr.sin_port = htons(53);
            addr.sin_addr.s_addr = inet_addr("192.168.4.1");
            timeval timeout{1, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
                while (enabled.load()) {
                    uint8_t packet[512]; sockaddr_in peer{}; socklen_t size = sizeof(peer);
                    int n = recvfrom(fd, packet, sizeof(packet), 0, reinterpret_cast<sockaddr *>(&peer), &size);
                    if (n <= 0) continue;
                    auto reply = captiveDnsReply(packet, n, sizeof(packet), addr.sin_addr.s_addr);
                    if (reply) sendto(fd, packet, reply, 0, reinterpret_cast<sockaddr *>(&peer), size);
                }
            }
            close(fd);
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}
void captiveDnsSetEnabled(bool value)
{
    enabled.store(value);
    if (value && !task) xTaskCreatePinnedToCore(run, "portal_dns", 3072, nullptr, 2, &task, 0);
}
