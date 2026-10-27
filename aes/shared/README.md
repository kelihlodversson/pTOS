# Shared AES code

This directory holds ordinary C sources reused by native AES, standalone GEM
programs, or libaes. Compile these sources into each consumer; do not include
implementation files from other C files.

- `gemrender*`, `gemicon.c` and `gemgraphutil.c`: original object painting and GSX drawing.
- `gemformkey.c`: original form field search and keyboard decisions, with
  native and userspace bindings providing default-button drawing.
- `gemlayout.c`: native resource-coordinate scaling and form centering with
  explicit workstation metrics, shared with libaes.
- `gemobjtree*` and `gemobjwalk.c`: object-tree editing, coordinates and traversal.
- `gemrslib*` and `rsload*`: resource handling and the configured resource loaders,
  already reused by native AES and the standalone desktop. Moving them here does
  not add the pending standard `rsrc_*` adapters to libaes.

Native AES wrappers and kernel validation remain in the parent directory.
The shared renderer's internal headers declare its interfaces and select the
build's drawing context. Native builds retain AES globals; libaes supplies
`lib/aes/render_bindings.h` and its private per-call context. Shared code must
keep application pointers and callbacks in userspace when built into libaes.
Userspace fills, borders, crossed lines and expanded shadows retain LONG
geometry through clipping; the native bindings keep their original WORD
interfaces and VDI point conversion.

Future reusable resource, object and graphics routines belong here too, with
consumer-specific transport and ownership code kept outside this directory.
