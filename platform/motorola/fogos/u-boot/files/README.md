# fogos U-Boot overlay (experimental)

No board-specific defconfig or device-tree overlay is provided yet. Do not add guessed RAM addresses, GPIOs, regulators, or clock mappings.

The current GitHub Actions job builds the generic upstream `qcom_defconfig` only to validate the toolchain and shared HakiOS command integration. It does not build or validate a Moto G34 boot image.

Required before a real board build:
- SM6375/Blair GCC clock and reset support
- validated G34 pinctrl/TLMM and PMIC/regulator initialization
- board DTS and reserved-memory map
- USB device-controller and UFS support
- verified ABL handoff and recovery procedure
