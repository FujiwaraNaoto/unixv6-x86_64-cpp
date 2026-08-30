#ifndef PCI_HPP
#define PCI_HPP

#include <cstdint>
#include <array>
#include <optional>

struct PCIBarInfo
{
    uint64_t base_address;
    uint64_t size;
    uint8_t is_io_space : 1;
    uint8_t is_64bit : 1;
    uint8_t is_prefetchable : 1;
    uint8_t valid : 1;
};

struct PCIDevice
{
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    std::array<PCIBarInfo, 6> bars;
};


namespace PCI
{
constexpr uint16_t CONFIG_ADDRESS_PORT = 0xCF8;
constexpr uint16_t CONFIG_DATA_PORT    = 0xCFC;

uint32_t read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

std::optional<PCIDevice> pci_device_exists(uint16_t vendor_id, uint16_t device_id);
void enable_bus_master(const PCIDevice &dev);
} // namespace PCI

#endif // PCI_HPP
