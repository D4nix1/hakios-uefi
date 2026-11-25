# Motorola Moto G34 5G — fogos

**Status: EXPERIMENTAL BOARD SCAFFOLD — NOT A BOOTABLE PHONE IMAGE.**

This profile targets the Motorola Moto G34 5G (codename `fogos`), Qualcomm SM6375 / Blair. It includes an experimental U-Boot defconfig and a minimal board-identity DTS. Those files are enough to attempt a cross-compilation, but they do not provide complete hardware initialization.

## What GitHub Actions builds

GitHub Actions uses upstream U-Boot v2026.07, applies the shared HakiOS integration and fogos overlay, and compiles `motorola_fogos_defconfig`. The artifact is an experimental U-Boot binary for static inspection only.

**It is not an Android `boot.img`, has not been tested on hardware, and must not be flashed or booted on the phone.**

## Why hardware boot remains blocked

- No confirmed SM6375/Blair GCC clock/reset driver is included in the pinned U-Boot tree.
- G34-specific pinctrl/TLMM, PMIC, regulators and early initialization remain incomplete.
- The minimal DTS declares board identity but intentionally does not invent RAM banks or reserved regions.
- USB device-controller operation, UFS access and ABL RAM-boot handoff remain unverified.
- The existing SM6350 clock driver is not assumed compatible with SM6375.

## Target identity

- Device codename: `fogos`
- Product: Motorola Moto G34 5G
- SoC: Qualcomm SM6375 / Blair
- U-Boot baseline: v2026.07, commit `ece349ade2973e220f524ce59e59711cc919263f`

## Validation checklist

- [ ] Confirm exact board ID/SKU and RAM topology
- [ ] Implement and validate SM6375 GCC clock/reset support
- [ ] Validate pinctrl, PMIC, regulators and required early init
- [ ] Establish device tree and reserved-memory layout from trusted G34 sources
- [ ] Verify ABL RAM-boot handoff and recovery path
- [ ] Validate USB device mode and storage path
- [ ] Complete the UFS/storage path
- [ ] Build and review the board-specific image
- [ ] Test RAM boot with serial logs and a known recovery route

No phone partitions have been written.
