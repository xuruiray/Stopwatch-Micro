#include <cstddef>
#include <cstdint>

#include <esp_gatt_defs.h>
#include <esp_gatts_api.h>
#include <esp_log.h>

namespace {

constexpr const char* Tag = "CodexMicro-GATT";

bool hasUuid16(const esp_attr_desc_t& attribute, uint16_t expected)
{
    if (attribute.uuid_length != ESP_UUID_LEN_16 || attribute.uuid_p == nullptr) {
        return false;
    }
    const uint16_t actual = static_cast<uint16_t>(attribute.uuid_p[0]) |
                            (static_cast<uint16_t>(attribute.uuid_p[1]) << 8U);
    return actual == expected;
}

bool isWritableCharacteristic(const esp_gatts_attr_db_t& declaration)
{
    const esp_attr_desc_t& attribute = declaration.att_desc;
    if (!hasUuid16(attribute, ESP_GATT_UUID_CHAR_DECLARE) || attribute.value == nullptr ||
        attribute.length < 1) {
        return false;
    }
    const uint8_t properties = attribute.value[0];
    return (properties & (ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR)) != 0;
}

}  // namespace

extern "C" esp_err_t __real_esp_ble_gatts_create_attr_tab(
    const esp_gatts_attr_db_t* gatts_attr_db, esp_gatt_if_t gatts_if, uint16_t max_nb_attr,
    uint8_t srvc_inst_id);

extern "C" void codex_micro_hid_gatt_compat_link_anchor()
{
}

extern "C" esp_err_t __wrap_esp_ble_gatts_create_attr_tab(
    const esp_gatts_attr_db_t* gatts_attr_db, esp_gatt_if_t gatts_if, uint16_t max_nb_attr,
    uint8_t srvc_inst_id)
{
    // node-hid passes the non-zero Report ID in its 64-byte SetReport buffer.
    // macOS forwards all 64 bytes to the BLE Output Report characteristic. The
    // ESP-IDF HID helper sizes that characteristic from the 63-byte report body,
    // so the ATT server rejects the otherwise valid write with error 0x0D
    // (Invalid Attribute Value Length). Keep the HID descriptor unchanged and
    // widen only the writable HID Report characteristic by the Report ID byte.
    if (gatts_attr_db != nullptr) {
        auto* mutable_db = const_cast<esp_gatts_attr_db_t*>(gatts_attr_db);
        for (uint16_t index = 1; index < max_nb_attr; ++index) {
            esp_attr_desc_t& attribute = mutable_db[index].att_desc;
            if (hasUuid16(attribute, ESP_GATT_UUID_HID_REPORT) &&
                isWritableCharacteristic(mutable_db[index - 1]) && attribute.max_length == 63) {
                attribute.max_length = 64;
                ESP_LOGI(Tag, "expanded HID output report characteristic to 64 bytes");
            }
        }
    }

    return __real_esp_ble_gatts_create_attr_tab(gatts_attr_db, gatts_if, max_nb_attr,
                                                srvc_inst_id);
}
