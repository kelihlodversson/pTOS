# #177 Shutdown IRQ Gate Report

## Change

`dwc2_irq_handler()` returns immediately after recognizing a host-channel or
SOF interrupt when `priv->shutting_down` is set. It therefore does not read or
acknowledge async channel interrupts, invoke callbacks, schedule a slot, or
start DMA while `usb_lowlevel_stop()` is draining slots. The shutdown drain
remains responsible for observing `CHHLTD` and releasing each slot.

The async contract test now requires this gate to precede async HAINT handling.

## Verification

- `sh tools/test-dwc2-async-contract.sh`: passed (first run failed before the
  gate was added).
- `make gitready`: passed.
- `git diff --check`: passed.
- `make rpi1_defconfig && make clean && make obj/ucd_dwc2.o`: passed.
- `make rpi2_defconfig && make clean && make obj/ucd_dwc2.o`: passed.
- `make rpi2-sparse_defconfig && make clean && make obj/ucd_dwc2.o`: passed.
- `make rpi3_defconfig && make clean && make obj/ucd_dwc2.o`: passed.
- `make rpi4_defconfig`: DWC2 excluded by configuration; RPi 4 uses xHCI.

The object builds retain the existing unused `hcd_name` warning. Make commands
also retain existing target-override warnings.

## Concerns

No emulator boot test was run: this is an IRQ/teardown state gate and the
requested verification was the contract test plus feasible DWC2 object builds.
