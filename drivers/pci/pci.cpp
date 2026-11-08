#include "pci.hpp"

#include <cstdint>
#include <optional>
#include "io.hpp"

namespace
{
uint32_t shift_left(uint32_t value, int shift)
{
    return value << shift;
}

uint32_t make_pci_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return shift_left(static_cast<uint32_t>(bus), 16) | shift_left(static_cast<uint32_t>(device), 11) |
           shift_left(static_cast<uint32_t>(function), 8) | shift_left(static_cast<uint32_t>(offset & 0xFC), 0) |
           shift_left(1, 31); // Enable bit
}
} // namespace


namespace PCI
{
uint32_t read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t address = make_pci_address(bus, device, function, offset);

    io::out32b(CONFIG_ADDRESS_PORT, address);
    return io::in32b(CONFIG_DATA_PORT);
}

void write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t address = make_pci_address(bus, device, function, offset);
    io::out32b(CONFIG_ADDRESS_PORT, address);
    io::out32b(CONFIG_DATA_PORT, value);

    // Ensure the write is posted
    io::in32b(CONFIG_DATA_PORT);
}


std::optional<PCIDevice> pci_device_exists(uint16_t vendor_id, uint16_t device_id)
{
    for (int bus = 0; bus < (1 << 8); bus++)
    {
        for (int device = 0; device < (1 << 5); device++)
        {
            for (int function = 0; function < (1 << 3); function++)
            {
                uint32_t data = read_config(bus, device, function, 0);
                uint16_t vid  = data & 0xFFFF;
                uint16_t did  = (data >> 16) & 0xFFFF;
                if (vid == vendor_id && did == device_id)
                {
                    PCIDevice pci_device{};
                    pci_device.bus       = bus;
                    pci_device.device    = static_cast<uint8_t>(device & 0x1F);
                    pci_device.function  = function;
                    pci_device.vendor_id = vid;
                    pci_device.device_id = did;

                    // Read BARs
                    for (int bar_index = 0; bar_index < 6; bar_index++)
                    {
                        uint32_t bar_data    = read_config(bus, pci_device.device, function, 0x10 + bar_index * 4);
                        PCIBarInfo &bar_info = pci_device.bars[bar_index];
                        bar_info.is_io_space = (bar_data & 0x1) != 0;
                        // フラグ幅が違う: I/O BAR は下位2bit、MMIO BAR は下位4bitがフラグ。
                        // I/O BAR に 0xFFFFFFF0 を掛けるとポート番号の bit2-3 が消える。
                        bar_info.base_address    = bar_info.is_io_space ? (bar_data & 0xFFFFFFFCu)
                                                                        : (bar_data & 0xFFFFFFF0u);
                        bar_info.is_64bit        = (bar_data & 0x4) != 0;
                        bar_info.is_prefetchable = (bar_data & 0x8) != 0;
                        bar_info.valid           = true;
                    }
                    return pci_device;
                }
            }
        }
    }
    return std::nullopt;
}

void enable_bus_master(const PCIDevice &dev)
{
    uint32_t command = read_config(dev.bus, dev.device, dev.function, 0x04);
    command |= (1 << 2); // Set the Bus Master Enable bit
    write_config(dev.bus, dev.device, dev.function, 0x04, command);
}
} // namespace PCI
