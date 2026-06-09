#pragma once

// Bridges ediabasx-embedded (C library) into the ESP-IDF firmware.
// Provides ESP-side implementations of:
//   - edxn_transport_t vtable, dispatching to the selected transport
//     component (K-line / CAN / IBUS / DoIP),
//   - sgbd_loader_t + table_loader_t callbacks reading from
//     /sdcard/data/ediabas/ecu/ via FATFS,
//   - log forwarding into the log_bus.
//
// See docs/firmware.md §8.

#include "esp_err.h"

esp_err_t ediabasx_platform_init(void);

// Filesystem path holding the BMW DATEN-disk ECU files (.prg / .grp).
// Returned as a constant so multiple components (loader, jsonrpc's
// listSgbd) agree on a single source of truth.
const char *ediabasx_platform_ecu_dir(void);

// ---- helpers used by jsonrpc (and anyone else that wants to inspect
//      a .prg without driving the VM) ---------------------------------
//
// load_prg() takes either a bare ECU name ("DDE602") or a filename with
// a `.prg`/`.grp` extension; resolves it case-insensitively under the
// ECU dir and parses the file into an `edxn_prg_t`. Caller MUST pair
// every successful return with `free_prg()` to release the buffers.

#include "ediabasx/prg.h"
#include "ediabasx/types.h"

edxn_error_t ediabasx_platform_load_prg(const char *name,
                                         edxn_prg_t **out_prg,
                                         uint8_t **out_bytes);

void ediabasx_platform_free_prg(edxn_prg_t *prg, uint8_t *bytes);

// Accessor for the shared EDIABAS instance. NULL until ediabasx_platform_init
// completes. The instance survives across job calls so loaded SGBDs and
// system result accumulators persist (mirrors the TS server's
// single-instance-with-serialized-job-queue model).
#include "ediabasx/ediabas.h"
edxn_ediabas_t *ediabasx_platform_eb(void);
