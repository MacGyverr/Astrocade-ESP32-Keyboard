#include "settings.h"

#include <cstring>

#include "nvs.h"

namespace
{
    constexpr char kNamespace[] = "astrocade";
    constexpr char kOverlayKey[] = "overlay";
    constexpr char kBleAddrKey[] = "ble_addr";
    constexpr char kBleTypeKey[] = "ble_type";
    constexpr char kBleNameKey[] = "ble_name";

    nvs_handle_t gNvs = 0;
    bool gReady = false;
}

bool settingsInit()
{
    if (gReady)
    {
        return true;
    }

    if (nvs_open(
            kNamespace,
            NVS_READWRITE,
            &gNvs) != ESP_OK)
    {
        return false;
    }

    gReady = true;
    return true;
}

OverlayMode settingsGetOverlayMode()
{
    if (!gReady)
    {
        return OverlayMode::Basic;
    }

    uint8_t value =
        static_cast<uint8_t>(OverlayMode::Basic);

    if (nvs_get_u8(
            gNvs,
            kOverlayKey,
            &value) != ESP_OK)
    {
        return OverlayMode::Basic;
    }

    switch (static_cast<OverlayMode>(value))
    {
        case OverlayMode::Basic:
            return OverlayMode::Basic;

        default:
            return OverlayMode::Basic;
    }
}

bool settingsSetOverlayMode(OverlayMode mode)
{
    if (!gReady)
    {
        return false;
    }

    if (nvs_set_u8(
            gNvs,
            kOverlayKey,
            static_cast<uint8_t>(mode)) != ESP_OK)
    {
        return false;
    }

    return nvs_commit(gNvs) == ESP_OK;
}

bool settingsGetPreferredBle(
    uint8_t address[6],
    uint8_t *addressType)
{
    if (!gReady ||
        address == nullptr ||
        addressType == nullptr)
    {
        return false;
    }

    size_t length = 6;

    if (nvs_get_blob(
            gNvs,
            kBleAddrKey,
            address,
            &length) != ESP_OK ||
        length != 6)
    {
        return false;
    }

    if (nvs_get_u8(
            gNvs,
            kBleTypeKey,
            addressType) != ESP_OK)
    {
        return false;
    }

    return true;
}

bool settingsSetPreferredBle(
    const uint8_t address[6],
    uint8_t addressType)
{
    if (!gReady || address == nullptr)
    {
        return false;
    }

    if (nvs_set_blob(
            gNvs,
            kBleAddrKey,
            address,
            6) != ESP_OK)
    {
        return false;
    }

    if (nvs_set_u8(
            gNvs,
            kBleTypeKey,
            addressType) != ESP_OK)
    {
        return false;
    }

    return nvs_commit(gNvs) == ESP_OK;
}

bool settingsGetPreferredBleName(
    char *name,
    std::size_t length)
{
    if (!gReady ||
        name == nullptr ||
        length == 0)
    {
        return false;
    }

    name[0] = '\0';

    std::size_t storedLength = length;

    if (nvs_get_str(
            gNvs,
            kBleNameKey,
            name,
            &storedLength) != ESP_OK)
    {
        name[0] = '\0';
        return false;
    }

    name[length - 1] = '\0';
    return name[0] != '\0';
}

bool settingsSetPreferredBleName(const char *name)
{
    if (!gReady)
    {
        return false;
    }

    if (name == nullptr ||
        name[0] == '\0')
    {
        (void)nvs_erase_key(gNvs, kBleNameKey);
        return nvs_commit(gNvs) == ESP_OK;
    }

    if (nvs_set_str(
            gNvs,
            kBleNameKey,
            name) != ESP_OK)
    {
        return false;
    }

    return nvs_commit(gNvs) == ESP_OK;
}

bool settingsClearPreferredBle()
{
    if (!gReady)
    {
        return false;
    }

    (void)nvs_erase_key(gNvs, kBleAddrKey);
    (void)nvs_erase_key(gNvs, kBleTypeKey);
    (void)nvs_erase_key(gNvs, kBleNameKey);

    return nvs_commit(gNvs) == ESP_OK;
}
