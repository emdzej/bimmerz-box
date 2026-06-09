#include "ediabasx_platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "ediabasx/ediabas.h"
#include "ediabasx/prg.h"
#include "ediabasx/vm.h"

#include "transport_kline.h"

static const char *TAG = "ediabasx_platform";

// BMW DATEN-disk root on the SD card. Single source of truth — the
// loader, listSgbd, and any future job-execution paths all read it
// from here so we don't drift across components.
#define ECU_DIR "/sdcard/data/ediabas/ecu"

// Single shared EDIABAS instance. Mirrors the TS server's
// single-instance-with-serialized-job-queue concurrency model.
// Allocated on PSRAM because the struct is ~50 KB and we have it in
// abundance.
static edxn_ediabas_t *s_eb = NULL;

// ---- helpers --------------------------------------------------------------

static uint8_t *read_whole_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    // .prg files for big ECUs can be tens of MB; deliberately allocate on
    // PSRAM so we don't blow the small internal heap.
    uint8_t *buf = heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { heap_caps_free(buf); return NULL; }
    *out_len = (size_t)n;
    return buf;
}

// Case-insensitive scan of `dir` for an entry matching `wanted`. On match,
// copies the actual on-disk filename into `out_name` and returns true.
// BMW SGBDs come from DOS-era disks with mixed case (MS420DS0.PRG vs
// ms420ds0.prg), so the dongle has to tolerate both.
static bool resolve_ci(const char *dir, const char *wanted,
                       char *out_name, size_t cap) {
    DIR *d = opendir(dir);
    if (!d) return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)) != NULL) {
        if (strcasecmp(e->d_name, wanted) == 0) {
            size_t l = strlen(e->d_name);
            if (l >= cap) l = cap - 1;
            memcpy(out_name, e->d_name, l);
            out_name[l] = '\0';
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static edxn_error_t load_prg_from_dir(const char *dir, const char *filename,
                                      edxn_prg_t **out_prg, uint8_t **out_bytes) {
    char actual[256];
    if (!resolve_ci(dir, filename, actual, sizeof(actual))) {
        return EDXN_ERR_FILE_IO;
    }
    char full[512];
    int n = snprintf(full, sizeof(full), "%s/%s", dir, actual);
    if (n <= 0 || (size_t)n >= sizeof(full)) return EDXN_ERR_FILE_IO;

    size_t len = 0;
    uint8_t *data = read_whole_file(full, &len);
    if (!data) return EDXN_ERR_FILE_IO;

    edxn_prg_t *prg = (edxn_prg_t *)heap_caps_calloc(1, sizeof(*prg),
                                                     MALLOC_CAP_SPIRAM);
    if (!prg) { heap_caps_free(data); return EDXN_ERR_NOMEM; }

    edxn_error_t err = edxn_prg_parse(prg, data, len);
    if (err != EDXN_OK) {
        heap_caps_free(prg);
        heap_caps_free(data);
        return err;
    }
    *out_prg = prg;
    *out_bytes = data;
    return EDXN_OK;
}

// ---- loader callbacks (registered with the VM) ----------------------------

static edxn_error_t sgbd_loader(void *ctx, const char *variant_name,
                                edxn_prg_t **out_prg, uint8_t **out_bytes) {
    const char *dir = (const char *)ctx;
    if (!dir || !variant_name) return EDXN_ERR_FILE_IO;
    char filename[256];
    int n = snprintf(filename, sizeof(filename), "%s.prg", variant_name);
    if (n <= 0 || (size_t)n >= sizeof(filename)) return EDXN_ERR_FILE_IO;
    return load_prg_from_dir(dir, filename, out_prg, out_bytes);
}

static edxn_error_t table_loader(void *ctx, const char *file_name,
                                 edxn_prg_t **out_prg, uint8_t **out_bytes) {
    const char *dir = (const char *)ctx;
    if (!dir || !file_name) return EDXN_ERR_FILE_IO;
    char filename[256];
    int n = snprintf(filename, sizeof(filename), "%s", file_name);
    if (n <= 0 || (size_t)n >= sizeof(filename)) return EDXN_ERR_FILE_IO;
    // The VM's tabsetex sometimes passes a plain name without extension;
    // append `.prg` so the case-insensitive resolver finds it.
    if (strchr(filename, '.') == NULL) {
        size_t l = strlen(filename);
        if (l + 4 >= sizeof(filename)) return EDXN_ERR_FILE_IO;
        strcat(filename, ".prg");
    }
    return load_prg_from_dir(dir, filename, out_prg, out_bytes);
}

// ---- public ---------------------------------------------------------------

const char *ediabasx_platform_ecu_dir(void) {
    return ECU_DIR;
}

edxn_ediabas_t *ediabasx_platform_eb(void) {
    return s_eb;
}

edxn_error_t ediabasx_platform_load_prg(const char *name,
                                         edxn_prg_t **out_prg,
                                         uint8_t **out_bytes) {
    if (!name || !out_prg || !out_bytes) return EDXN_ERR_FILE_IO;
    const char *dot = strrchr(name, '.');
    char filename[256];
    int n;

    // Caller supplied an explicit .prg or .grp extension → use it directly.
    if (dot && (strcasecmp(dot, ".prg") == 0 || strcasecmp(dot, ".grp") == 0)) {
        n = snprintf(filename, sizeof(filename), "%s", name);
        if (n <= 0 || (size_t)n >= sizeof(filename)) return EDXN_ERR_FILE_IO;
        return load_prg_from_dir(ECU_DIR, filename, out_prg, out_bytes);
    }

    // No extension — try .prg first (the common case for ECU variants),
    // then fall back to .grp (BMW group/variant-resolution files like
    // d_0080.grp). The web client sends the SGBD basename without
    // extension; without this fallback, group files silently return
    // "no jobs found".
    n = snprintf(filename, sizeof(filename), "%s.prg", name);
    if (n <= 0 || (size_t)n >= sizeof(filename)) return EDXN_ERR_FILE_IO;
    edxn_error_t err = load_prg_from_dir(ECU_DIR, filename, out_prg, out_bytes);
    if (err == EDXN_OK) return EDXN_OK;

    n = snprintf(filename, sizeof(filename), "%s.grp", name);
    if (n <= 0 || (size_t)n >= sizeof(filename)) return EDXN_ERR_FILE_IO;
    return load_prg_from_dir(ECU_DIR, filename, out_prg, out_bytes);
}

void ediabasx_platform_free_prg(edxn_prg_t *prg, uint8_t *bytes) {
    if (prg) {
        edxn_prg_free(prg);
        heap_caps_free(prg);
    }
    if (bytes) heap_caps_free(bytes);
}

esp_err_t ediabasx_platform_init(void) {
    if (s_eb != NULL) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    s_eb = heap_caps_calloc(1, sizeof(*s_eb), MALLOC_CAP_SPIRAM);
    if (s_eb == NULL) {
        ESP_LOGE(TAG, "failed to allocate edxn_ediabas_t in PSRAM");
        return ESP_ERR_NO_MEM;
    }

    edxn_error_t err = edxn_ediabas_init(s_eb);
    if (err != EDXN_OK) {
        ESP_LOGE(TAG, "edxn_ediabas_init failed: %d", (int)err);
        heap_caps_free(s_eb);
        s_eb = NULL;
        return ESP_FAIL;
    }

    // Register loaders so .grp variant swaps and tabsetex external-table
    // lookups can read .prg/.grp files from FATFS.
    edxn_ediabas_set_sgbd_loader(s_eb, sgbd_loader, (void *)ECU_DIR);
    edxn_ediabas_set_table_loader(s_eb, table_loader, (void *)ECU_DIR);

    // Existence check on the ECU directory — log a warning if it's
    // missing so the first failed listSgbd call has a hint in the log.
    struct stat st;
    if (stat(ECU_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        ESP_LOGW(TAG, "ECU dir not present: %s "
                      "(upload .prg/.grp files there to enable jobs)",
                 ECU_DIR);
    } else {
        ESP_LOGI(TAG, "ECU dir: %s", ECU_DIR);
    }

    ESP_LOGI(TAG, "edxn_ediabas_t allocated in PSRAM (%u bytes), VM ready",
             (unsigned)sizeof(*s_eb));

    // Bring up the K-line UART and attach its transport vtable to the VM
    // if the pins are wired on this board (Waveshare DEV-KIT has the
    // L9637D on GPIO 37/38). Failures here are non-fatal — the rest of
    // the firmware (HTTP, SGBD enumeration) keeps working.
    esp_err_t kerr = transport_kline_init();
    if (kerr == ESP_OK) {
        edxn_transport_t *kt = transport_kline_vtable();
        if (kt) {
            edxn_ediabas_set_transport(s_eb, kt);
            ESP_LOGI(TAG, "K-line transport registered with VM");
        }
    } else {
        ESP_LOGW(TAG, "transport_kline_init: %s — VM has no transport",
                 esp_err_to_name(kerr));
    }

    // TODO: log forwarding into log_bus.

    return ESP_OK;
}
