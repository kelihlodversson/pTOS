# Task 15 Documentation Sweep

- Updated `fs/pfs.h` to state that the bdos `x*` shims call `pfs_do_*()`
  when `CONF_WITH_PLUGGABLE_FS` is enabled.
- Updated `fs/fatfs_pfs.c` to describe its FAT cores, off-path wrappers, and
  ON-only vtable support without describing an adapter back into `bdos/`.
- Updated `fs/build.mk` and `fs/Kconfig` comments/help for the inverted
  dispatch layout.
- `doc/status.txt` does not mention pluggable filesystem dispatch wiring, so
  it was not changed.
- Verification: `make gitready` and a clean representative build are run
  before the task commit.
