#include "jsonrpc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "ediabasx_platform.h"
#include "http_static.h"
#include "transport_kline.h"
#include "kline_proto.h"

static const char *TAG = "jsonrpc";

// ---- dispatch table -------------------------------------------------------

typedef cJSON *(*jsonrpc_handler_fn)(const cJSON *params);

typedef struct {
    const char *method;
    jsonrpc_handler_fn handler;
} jsonrpc_method_t;

// `info` — server / dongle metadata. Matches the TS server's shape:
// { connected, clients, host, port, transport, sgbdPath }.
// `connected` reflects whether the server is running; we keep it true
// for the embedded path because there's no separate "connect to EDIABAS"
// step like the TS server has — the server is the EDIABAS host.
static cJSON *handle_info(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "connected", true);
    cJSON_AddNumberToObject(r, "clients", 0);
    cJSON_AddStringToObject(r, "host", "172.16.7.1");
    cJSON_AddNumberToObject(r, "port", 80);
    cJSON_AddStringToObject(r, "transport", "websocket");
    cJSON_AddStringToObject(r, "sgbdPath", ediabasx_platform_ecu_dir());
    return r;
}

// Sort comparator for cJSON array of {name, ext} objects.
// TS server uses `String.localeCompare`. For BMW filenames (ASCII), a
// plain strcmp on the canonical (with-extension) `name` is equivalent.
static int cmp_sgbd_entry(const void *a, const void *b) {
    const cJSON *ja = *(const cJSON **)a;
    const cJSON *jb = *(const cJSON **)b;
    const cJSON *na = cJSON_GetObjectItemCaseSensitive(ja, "name");
    const cJSON *nb = cJSON_GetObjectItemCaseSensitive(jb, "name");
    if (!cJSON_IsString(na) || !cJSON_IsString(nb)) return 0;
    return strcmp(na->valuestring, nb->valuestring);
}

// `init` — no per-connection setup needed yet (single client, no
// transport state). Mirror the TS server's `{ok:true}` ack so the
// web app moves past its bootstrap step.
static cJSON *handle_init(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

// `end` — same: no state to release yet.
static cJSON *handle_end(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    return r;
}

// `listJobs` { ecu } — load the .prg, return the job table.
// Shape: { jobs: [{name, argCount, resultCount}], tableCount }.
static cJSON *handle_listJobs(const cJSON *params) {
    cJSON *r = cJSON_CreateObject();
    cJSON *jobs = cJSON_AddArrayToObject(r, "jobs");
    cJSON_AddNumberToObject(r, "tableCount", 0);

    const cJSON *ecu = cJSON_GetObjectItemCaseSensitive(params, "ecu");
    if (!cJSON_IsString(ecu) || !ecu->valuestring) {
        return r;
    }

    edxn_prg_t *prg = NULL;
    uint8_t *bytes = NULL;
    edxn_error_t err = ediabasx_platform_load_prg(ecu->valuestring, &prg, &bytes);
    if (err != EDXN_OK || !prg) {
        ESP_LOGW(TAG, "listJobs: failed to load %s (err=%d)", ecu->valuestring, (int)err);
        return r;
    }

    for (size_t i = 0; i < prg->job_count; ++i) {
        const edxn_prg_job_t *j = &prg->jobs[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", j->name);
        cJSON_AddNumberToObject(o, "argCount", j->arg_count);
        cJSON_AddNumberToObject(o, "resultCount", j->result_count);
        cJSON_AddItemToArray(jobs, o);
    }
    cJSON_ReplaceItemInObjectCaseSensitive(
        r, "tableCount", cJSON_CreateNumber((double)prg->table_count));

    ediabasx_platform_free_prg(prg, bytes);
    return r;
}

// `listSgbd` — scan the configured ECU dir for .prg / .grp files.
// TS contract (ediabas-server.ts handleListSgbd):
//   - `name`: full on-disk filename, case preserved (`00dde502.prg`)
//   - `ext` : extension WITHOUT the leading dot, lowercase (`prg` / `grp`)
//   - sorted by String.localeCompare on `name`
static cJSON *handle_listSgbd(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON *sgbds = cJSON_AddArrayToObject(r, "sgbds");

    const char *dir = ediabasx_platform_ecu_dir();
    DIR *d = opendir(dir);
    if (!d) return r;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || dot == e->d_name) continue;
        // Skip past the dot — `ext` is lowercase, no leading dot.
        char ext_lower[8] = { 0 };
        const char *src = dot + 1;
        for (size_t i = 0; i < sizeof(ext_lower) - 1 && src[i]; ++i) {
            ext_lower[i] = (char)tolower((unsigned char)src[i]);
        }
        if (strcmp(ext_lower, "prg") != 0 && strcmp(ext_lower, "grp") != 0) {
            continue;
        }

        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", e->d_name);  // full filename, case as-on-disk
        cJSON_AddStringToObject(o, "ext", ext_lower);   // "prg" or "grp"
        cJSON_AddItemToArray(sgbds, o);
    }
    closedir(d);

    int n = cJSON_GetArraySize(sgbds);
    if (n > 1) {
        cJSON **arr = malloc(sizeof(*arr) * (size_t)n);
        if (arr) {
            for (int i = 0; i < n; ++i) arr[i] = cJSON_GetArrayItem(sgbds, i);
            qsort(arr, (size_t)n, sizeof(*arr), cmp_sgbd_entry);
            // Rebuild sorted: detach + reattach in order
            cJSON *new_sgbds = cJSON_CreateArray();
            for (int i = 0; i < n; ++i) {
                cJSON_DetachItemViaPointer(sgbds, arr[i]);
                cJSON_AddItemToArray(new_sgbds, arr[i]);
            }
            cJSON_ReplaceItemInObjectCaseSensitive(r, "sgbds", new_sgbds);
            free(arr);
        }
    }

    return r;
}

// `state` — { state: "ready" | "busy" | "break" | "error" }. The VM
// isn't driving yet, so always "ready".
static cJSON *handle_state(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "state", "ready");
    return r;
}

// `errorText` — last EDIABAS error text. Empty until ediabasx_platform
// is wired into the dispatch path.
static cJSON *handle_errorText(const cJSON *params) {
    (void)params;
    cJSON *r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "text", "");
    return r;
}

// ---- result + log + diagnostic stubs --------------------------------------
//
// The TS server holds these between job calls. We're not running jobs
// yet, so they're flat stubs returning the "no last result" / "no error"
// case. They exist solely so the web app doesn't bail on -32601 when it
// probes them on connect.

static cJSON *handle_resultSets(const cJSON *p)  { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddNumberToObject(r, "count", 0); return r; }
static cJSON *handle_resultText(const cJSON *p)  { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddStringToObject(r, "value", ""); return r; }
static cJSON *handle_resultInt(const cJSON *p)   { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddNumberToObject(r, "value", 0);  return r; }
static cJSON *handle_resultReal(const cJSON *p)  { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddNumberToObject(r, "value", 0);  return r; }
static cJSON *handle_resultBinary(const cJSON *p){ (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddArrayToObject(r, "value");      return r; }
static cJSON *handle_resultFormat(const cJSON *p){ (void)p; cJSON *r = cJSON_CreateObject(); /* format omitted */ return r; }

static cJSON *handle_errorCode(const cJSON *p)   { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddNumberToObject(r, "code", 0); return r; }
static cJSON *handle_break(const cJSON *p)       { (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true);  return r; }

// `log.subscribe` { level? } — accepts the subscription. Server-side
// log broadcast hasn't been wired through yet (log_bus → notifications);
// this acks the request so the client doesn't disconnect.
static cJSON *handle_logSubscribe(const cJSON *p) {
    const cJSON *lvl = cJSON_GetObjectItemCaseSensitive(p, "level");
    const char *level = (cJSON_IsString(lvl) && lvl->valuestring) ? lvl->valuestring : "info";
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddStringToObject(r, "level", level);
    return r;
}

static cJSON *handle_logUnsubscribe(const cJSON *p) {
    (void)p; cJSON *r = cJSON_CreateObject(); cJSON_AddBoolToObject(r, "ok", true); return r;
}

// Forward declaration — implementation in the K-line ident helpers
// section further down.
static size_t decode_bytes(const cJSON *j, uint8_t *out, size_t cap);

// Map edxn_result_type_t to the TS server's EdiabasResultType strings
// (defined in ediabasx-server's mapResultType): "text" / "integer" /
// "real" / "binary" / "long".
static const char *map_result_type(edxn_result_type_t t) {
    switch (t) {
        case EDXN_TYPE_INT:
        case EDXN_TYPE_CHAR:
        case EDXN_TYPE_BYTE:
        case EDXN_TYPE_WORD:
        case EDXN_TYPE_DWORD: return "integer";
        case EDXN_TYPE_LONG:  return "long";
        case EDXN_TYPE_FLOAT: return "real";
        case EDXN_TYPE_STRING: return "text";
        case EDXN_TYPE_BINARY: return "binary";
    }
    return "text";
}

// Render one result set as { result_name: { name, type, value }, ... }.
// Mirrors the EdiabasResultSet shape the TS server emits — keyed by the
// result name, with each entry carrying its declared type and value. The
// ediabasx-client / web-ui look up entries by name and read `.value` /
// `.type` (see EdiabasResultEntry in ediabasx-client/src).
static cJSON *render_result_set(const edxn_result_set_t *set) {
    cJSON *o = cJSON_CreateObject();
    for (size_t i = 0; i < set->count; ++i) {
        const edxn_result_entry_t *e = &set->entries[i];
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "name", e->name);
        cJSON_AddStringToObject(entry, "type", map_result_type(e->type));
        switch (e->type) {
            case EDXN_TYPE_INT:
            case EDXN_TYPE_LONG:
            case EDXN_TYPE_CHAR:
            case EDXN_TYPE_BYTE:
            case EDXN_TYPE_WORD:
            case EDXN_TYPE_DWORD:
                cJSON_AddNumberToObject(entry, "value", (double)e->value.i);
                break;
            case EDXN_TYPE_FLOAT:
                cJSON_AddNumberToObject(entry, "value", e->value.f);
                break;
            case EDXN_TYPE_STRING: {
                char stack[256];
                char *buf = stack;
                bool heap = false;
                if (e->value.bin.len + 1 > sizeof(stack)) {
                    buf = malloc(e->value.bin.len + 1);
                    if (!buf) { cJSON_Delete(entry); continue; }
                    heap = true;
                }
                memcpy(buf, e->value.bin.data, e->value.bin.len);
                buf[e->value.bin.len] = '\0';
                cJSON_AddStringToObject(entry, "value", buf);
                if (heap) free(buf);
                break;
            }
            case EDXN_TYPE_BINARY: {
                cJSON *arr = cJSON_AddArrayToObject(entry, "value");
                for (size_t k = 0; k < e->value.bin.len; ++k) {
                    cJSON_AddItemToArray(arr, cJSON_CreateNumber(e->value.bin.data[k]));
                }
                break;
            }
        }
        cJSON_AddItemToObject(o, e->name, entry);
    }
    return o;
}

// `job` { ecu, job, params? } — run a BEST2 job through the embedded VM.
// Mirrors the TS server's handleJob shape: { sets: [{...system}, {...set1}, ...] }.
//
// The shared edxn_ediabas_t survives across calls so loaded SGBDs are
// cached and INITIALISIERUNG/IDENT only fire on the first job for a given
// SGBD. A different `ecu` triggers a fresh load_sgbd (which takes ownership
// of the new prg buffer — old buffer is freed by the VM internally).
static cJSON *handle_job(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    cJSON *sets_a = cJSON_AddArrayToObject(r, "sets");

    const cJSON *ej = cJSON_GetObjectItemCaseSensitive(p, "ecu");
    const cJSON *jj = cJSON_GetObjectItemCaseSensitive(p, "job");
    const cJSON *aj = cJSON_GetObjectItemCaseSensitive(p, "params");

    if (!cJSON_IsString(ej) || !ej->valuestring ||
        !cJSON_IsString(jj) || !jj->valuestring) {
        cJSON_AddStringToObject(r, "error", "missing ecu/job");
        return r;
    }

    edxn_ediabas_t *eb = ediabasx_platform_eb();
    if (!eb) {
        cJSON_AddStringToObject(r, "error", "ediabas not initialized");
        return r;
    }

    // Load SGBD if it differs from what's currently loaded. The VM
    // accepts either bare names ("MS430") or filenames with extension;
    // ediabasx_platform_load_prg handles both.
    const char *want = ej->valuestring;
    if (strcasecmp(eb->sgbd_name, want) != 0) {
        edxn_prg_t *prg = NULL;
        uint8_t *bytes = NULL;
        edxn_error_t lerr = ediabasx_platform_load_prg(want, &prg, &bytes);
        if (lerr != EDXN_OK) {
            ESP_LOGW(TAG, "job: failed to load SGBD %s (err=%d)", want, (int)lerr);
            cJSON_AddStringToObject(r, "error", "sgbd load failed");
            return r;
        }
        // load_sgbd takes ownership of prg + bytes on success. On
        // failure it returns those buffers untouched — we then free them.
        lerr = edxn_ediabas_load_sgbd(eb, prg, bytes, want);
        if (lerr != EDXN_OK) {
            ESP_LOGW(TAG, "job: load_sgbd %s failed (err=%d)", want, (int)lerr);
            ediabasx_platform_free_prg(prg, bytes);
            cJSON_AddStringToObject(r, "error", "sgbd attach failed");
            return r;
        }
    }

    // Args may be a string ("0x12;0x04") or a binary array of params.
    const char *args = "";
    uint8_t bin[256];
    size_t bin_len = 0;
    if (cJSON_IsString(aj) && aj->valuestring) {
        args = aj->valuestring;
    } else if (cJSON_IsArray(aj)) {
        bin_len = decode_bytes(aj, bin, sizeof(bin));
    }

    edxn_error_t err = (bin_len > 0)
        ? edxn_ediabas_exec_data(eb, jj->valuestring, args, bin, bin_len)
        : edxn_ediabas_exec(eb, jj->valuestring, args);
    if (err != EDXN_OK) {
        ESP_LOGW(TAG, "job: exec %s failed (err=%d)", jj->valuestring, (int)err);
        cJSON_AddStringToObject(r, "error", "exec failed");
        cJSON_AddNumberToObject(r, "errorCode", (double)err);
        return r;
    }

    size_t set_count = 0;
    const edxn_result_set_t *sets = edxn_ediabas_get_sets(eb, &set_count);
    for (size_t i = 0; i < set_count; ++i) {
        cJSON_AddItemToArray(sets_a, render_result_set(&sets[i]));
    }
    return r;
}

// `getJobMetadata` { ecu, job } — TS shape is { name, comment?, args[], results[] }.
// The C library doesn't yet parse the arg/result metadata section; return
// the name with empty arg/result lists so the contract is shape-correct.
static cJSON *handle_getJobMetadata(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    const cJSON *job = cJSON_GetObjectItemCaseSensitive(p, "job");
    cJSON_AddStringToObject(r, "name",
                            (cJSON_IsString(job) && job->valuestring) ? job->valuestring : "");
    cJSON_AddArrayToObject(r, "args");
    cJSON_AddArrayToObject(r, "results");
    return r;
}

// `disassembleJob` { ecu, job } — TS returns { lines: string[] }. The
// embedded disassembler isn't wired yet; stub to a placeholder line so
// the IDE-style "no bytecode" message renders rather than an error.
static cJSON *handle_disassembleJob(const cJSON *p) {
    (void)p;
    cJSON *r = cJSON_CreateObject();
    cJSON *lines = cJSON_AddArrayToObject(r, "lines");
    cJSON_AddItemToArray(lines, cJSON_CreateString("(disassembly not yet supported on the embedded server)"));
    return r;
}

// `klineHoldTx { level: 0|1, holdMs?: number }` — drive GPIO 37 to the
// requested level for `holdMs` ms (default 5000), then restore. Used
// for offline multimeter probing along the chain: ESP GPIO 37 →
// mikroBUS TX → L9637D pin 3 → K-line → L9637D pin 4 → GPIO 38.
static cJSON *handle_klineHoldTx(const cJSON *p) {
    int level = 0;
    uint32_t hold_ms = 5000;
    const cJSON *lj = cJSON_GetObjectItemCaseSensitive(p, "level");
    if (cJSON_IsNumber(lj)) level = lj->valueint ? 1 : 0;
    const cJSON *hj = cJSON_GetObjectItemCaseSensitive(p, "holdMs");
    if (cJSON_IsNumber(hj) && hj->valueint > 0) hold_ms = (uint32_t)hj->valueint;

    int rx = -1;
    esp_err_t err = transport_kline_hold_tx(level, hold_ms, &rx);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "level", level);
    cJSON_AddNumberToObject(r, "holdMs", (double)hold_ms);
    cJSON_AddNumberToObject(r, "rxDuringHold", rx);
    cJSON_AddStringToObject(r, "ok",
                            err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineWireTest` — hardware-level GPIO toggle that bypasses UART
// entirely. Drives TX high then low as plain GPIO and reads RX. With a
// healthy L9637D + wiring, RX mirrors TX. Used to disambiguate UART
// routing from physical wiring.
static cJSON *handle_klineWireTest(const cJSON *p) {
    (void)p;
    transport_kline_wire_result_t res = { 0 };
    esp_err_t err = transport_kline_wire_test(&res);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "rxWhenTxHigh", res.rx_when_tx_high);
    cJSON_AddNumberToObject(r, "rxWhenTxLow",  res.rx_when_tx_low);
    cJSON_AddBoolToObject(r,   "loopOk",       res.loop_ok);
    cJSON_AddStringToObject(r, "ok",
                            err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineProbe` { tx?: number[], timeoutMs?: number, baud?: number }
//
// Debug helper: pushes bytes onto K-line and reports everything that
// arrives on RX during the wait window. With L9637D wired but no ECU,
// the half-duplex bus loops TX back to RX — you should see your own
// bytes echoed. When an ECU is connected, the echo is followed by the
// ECU's response.
//
// Returns: { tx: number[], rx: number[], rxHex: string }
static cJSON *handle_klineProbe(const cJSON *p) {
    uint8_t tx[32];
    size_t tx_len = 0;

    const cJSON *txj = cJSON_GetObjectItemCaseSensitive(p, "tx");
    if (cJSON_IsArray(txj)) {
        cJSON *e;
        cJSON_ArrayForEach(e, txj) {
            if (tx_len >= sizeof(tx)) break;
            if (cJSON_IsNumber(e)) tx[tx_len++] = (uint8_t)(e->valueint & 0xFF);
        }
    }
    if (tx_len == 0) {
        // Sensible default: 4 byte alternating pattern; trivially
        // distinguishable from noise.
        tx[0] = 0xAA; tx[1] = 0x55; tx[2] = 0x0F; tx[3] = 0xF0;
        tx_len = 4;
    }

    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(p, "baud");
    if (cJSON_IsNumber(bj) && bj->valueint > 0) {
        transport_kline_set_baud((uint32_t)bj->valueint);
    }

    uint32_t timeout_ms = 200;
    const cJSON *tmj = cJSON_GetObjectItemCaseSensitive(p, "timeoutMs");
    if (cJSON_IsNumber(tmj) && tmj->valueint > 0) {
        timeout_ms = (uint32_t)tmj->valueint;
    }

    // Diagnostic: enable internal TX→RX loopback in the UART peripheral
    // for this probe so we can isolate the UART driver from the L9637D
    // wiring. Always reset to OFF afterwards so it doesn't bleed into
    // subsequent real ECU probes.
    bool loopback = false;
    const cJSON *lj = cJSON_GetObjectItemCaseSensitive(p, "loopback");
    if (cJSON_IsBool(lj) && cJSON_IsTrue(lj)) {
        loopback = true;
        transport_kline_set_loopback(true);
    }

    uint8_t rx[256] = { 0 };
    size_t rx_len = 0;
    esp_err_t err = transport_kline_raw(tx, tx_len, rx, sizeof(rx), &rx_len, timeout_ms);

    // Always disable loopback after the probe completes.
    if (loopback) transport_kline_set_loopback(false);

    cJSON *r = cJSON_CreateObject();
    cJSON *txa = cJSON_AddArrayToObject(r, "tx");
    for (size_t i = 0; i < tx_len; ++i) cJSON_AddItemToArray(txa, cJSON_CreateNumber(tx[i]));
    cJSON *rxa = cJSON_AddArrayToObject(r, "rx");
    for (size_t i = 0; i < rx_len; ++i) cJSON_AddItemToArray(rxa, cJSON_CreateNumber(rx[i]));

    // NUL-init: rx_len=0 leaves the buffer untouched by the loop, and an
    // uninitialised hex[] would let cJSON's strdup copy stack garbage
    // (including bytes >= 0x80, breaking the WS frame's UTF-8 contract).
    char hex[3 * 256 + 1] = { 0 };
    int o = 0;
    for (size_t i = 0; i < rx_len && o + 3 < (int)sizeof(hex); ++i) {
        o += snprintf(hex + o, sizeof(hex) - o, "%02X ", rx[i]);
    }
    if (o > 0 && hex[o - 1] == ' ') hex[o - 1] = '\0';
    cJSON_AddStringToObject(r, "rxHex", hex);
    cJSON_AddStringToObject(r, "ok",
                            err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// ---- K-line ident helpers -------------------------------------------------

// Decode a JSON byte sequence into `out`. Accepts either:
//   - a numeric array: [0x12, 0x04, 0x00]
//   - a hex string:    "12 04 00"  (whitespace optional)
// Returns the number of bytes parsed, capped at `cap`. Returns 0 on no
// input / parse failure.
static size_t decode_bytes(const cJSON *j, uint8_t *out, size_t cap) {
    if (!j || !out || cap == 0) return 0;
    size_t n = 0;
    if (cJSON_IsArray(j)) {
        cJSON *e;
        cJSON_ArrayForEach(e, j) {
            if (n >= cap) break;
            if (cJSON_IsNumber(e)) out[n++] = (uint8_t)(e->valueint & 0xFF);
        }
        return n;
    }
    if (cJSON_IsString(j) && j->valuestring) {
        const char *s = j->valuestring;
        while (*s && n < cap) {
            while (*s && !isxdigit((unsigned char)*s)) s++;
            if (!*s) break;
            char buf[3] = { 0 };
            buf[0] = *s++;
            if (isxdigit((unsigned char)*s)) buf[1] = *s++;
            out[n++] = (uint8_t)strtoul(buf, NULL, 16);
        }
        return n;
    }
    return 0;
}

// Hex-encode `len` bytes from `src` into `dst` (`dst_cap` must be ≥ 3*len+1).
static void encode_hex(const uint8_t *src, size_t len, char *dst, size_t dst_cap) {
    size_t o = 0;
    if (dst_cap == 0) return;
    dst[0] = '\0';
    for (size_t i = 0; i < len && o + 3 < dst_cap; ++i) {
        o += snprintf(dst + o, dst_cap - o, "%02X ", src[i]);
    }
    if (o > 0 && dst[o - 1] == ' ') dst[o - 1] = '\0';
}

// Encode `len` bytes from `src` into a cJSON number array.
static cJSON *bytes_to_json_array(const uint8_t *src, size_t len) {
    cJSON *a = cJSON_CreateArray();
    for (size_t i = 0; i < len; ++i) {
        cJSON_AddItemToArray(a, cJSON_CreateNumber(src[i]));
    }
    return a;
}

// `klineSetBaud { baud }` — runtime baud switch (no protocol change).
static cJSON *handle_klineSetBaud(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(p, "baud");
    if (!cJSON_IsNumber(bj) || bj->valueint <= 0) {
        cJSON_AddStringToObject(r, "ok", "ESP_ERR_INVALID_ARG");
        return r;
    }
    esp_err_t err = transport_kline_set_baud((uint32_t)bj->valueint);
    cJSON_AddNumberToObject(r, "baud", bj->valueint);
    cJSON_AddStringToObject(r, "ok", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineSetParity { parity }` — "none"|"even"|"odd".
static cJSON *handle_klineSetParity(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();
    const cJSON *pj = cJSON_GetObjectItemCaseSensitive(p, "parity");
    if (!cJSON_IsString(pj) || !pj->valuestring) {
        cJSON_AddStringToObject(r, "ok", "ESP_ERR_INVALID_ARG");
        return r;
    }
    esp_err_t err = transport_kline_set_parity(pj->valuestring);
    cJSON_AddStringToObject(r, "parity", pj->valuestring);
    cJSON_AddStringToObject(r, "ok", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineFastInit { breakMs?, idleMs? }` — send KWP2000 fast-init pulse.
static cJSON *handle_klineFastInit(const cJSON *p) {
    uint32_t br = 0, idle = 0;
    const cJSON *bj = cJSON_GetObjectItemCaseSensitive(p, "breakMs");
    const cJSON *ij = cJSON_GetObjectItemCaseSensitive(p, "idleMs");
    if (cJSON_IsNumber(bj) && bj->valueint > 0) br   = (uint32_t)bj->valueint;
    if (cJSON_IsNumber(ij) && ij->valueint > 0) idle = (uint32_t)ij->valueint;

    esp_err_t err = transport_kline_send_fast_init(br, idle);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "breakMs", br ? br : KLINE_FAST_INIT_BREAK_MS);
    cJSON_AddNumberToObject(r, "idleMs",  idle ? idle : KLINE_FAST_INIT_IDLE_MS);
    cJSON_AddStringToObject(r, "ok", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineSlowInit { value?, bitTimeMs?, baudAfter?, parityAfter?, readKeyBytesMs? }`
//
// Send a 5-baud slow-init pulse for `value` (default 0x33 — common BMW
// KWP wake-up address), optionally switch baud+parity for the post-init
// communication, then optionally read incoming key bytes for up to
// `readKeyBytesMs` ms (default 1000). Returns the bytes received as both
// raw and hex.
static cJSON *handle_klineSlowInit(const cJSON *p) {
    uint8_t value = 0x33;
    uint32_t bit_ms = 0;
    uint32_t read_ms = 1000;

    const cJSON *vj  = cJSON_GetObjectItemCaseSensitive(p, "value");
    const cJSON *btj = cJSON_GetObjectItemCaseSensitive(p, "bitTimeMs");
    const cJSON *rmj = cJSON_GetObjectItemCaseSensitive(p, "readKeyBytesMs");
    const cJSON *baj = cJSON_GetObjectItemCaseSensitive(p, "baudAfter");
    const cJSON *paj = cJSON_GetObjectItemCaseSensitive(p, "parityAfter");

    if (cJSON_IsNumber(vj))  value   = (uint8_t)(vj->valueint & 0xFF);
    if (cJSON_IsNumber(btj) && btj->valueint > 0) bit_ms  = (uint32_t)btj->valueint;
    if (cJSON_IsNumber(rmj) && rmj->valueint >= 0) read_ms = (uint32_t)rmj->valueint;

    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "value", value);
    cJSON_AddNumberToObject(r, "bitTimeMs", bit_ms ? bit_ms : KLINE_FIVE_BAUD_BIT_TIME_MS);

    esp_err_t err = transport_kline_send_5baud(value, bit_ms);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(r, "ok", esp_err_to_name(err));
        return r;
    }

    // Optional comms reconfiguration before reading key bytes.
    if (cJSON_IsNumber(baj) && baj->valueint > 0) {
        transport_kline_set_baud((uint32_t)baj->valueint);
        cJSON_AddNumberToObject(r, "baudAfter", baj->valueint);
    }
    if (cJSON_IsString(paj) && paj->valuestring) {
        transport_kline_set_parity(paj->valuestring);
        cJSON_AddStringToObject(r, "parityAfter", paj->valuestring);
    }

    uint8_t rx[32] = { 0 };
    size_t rx_len = 0;
    if (read_ms > 0) {
        transport_kline_read(rx, sizeof(rx), &rx_len, read_ms);
    }
    cJSON_AddItemToObject(r, "rx", bytes_to_json_array(rx, rx_len));
    char hex[3 * sizeof(rx) + 1] = { 0 };
    encode_hex(rx, rx_len, hex, sizeof(hex));
    cJSON_AddStringToObject(r, "rxHex", hex);
    cJSON_AddStringToObject(r, "ok", "ok");
    return r;
}

// `klineKwpRequest { ecu, tester, payload, timeoutMs?, telEndMs?, regenMs?, baud? }`
//
// Thin wrapper around transport_kline_kwp_transact (the canonical TS-port
// of Kwp2000Session.sendRequest). Exposes the wire-level frame in `txHex`
// for diagnostics; the parsed BEST2 payload portion is returned under
// `payload` / `payloadHex` for convenience.
static cJSON *handle_klineKwpRequest(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();

    const cJSON *ej  = cJSON_GetObjectItemCaseSensitive(p, "ecu");
    const cJSON *tj  = cJSON_GetObjectItemCaseSensitive(p, "tester");
    const cJSON *pj  = cJSON_GetObjectItemCaseSensitive(p, "payload");
    const cJSON *bj  = cJSON_GetObjectItemCaseSensitive(p, "baud");
    const cJSON *tmj = cJSON_GetObjectItemCaseSensitive(p, "timeoutMs");
    const cJSON *tej = cJSON_GetObjectItemCaseSensitive(p, "telEndMs");
    const cJSON *rgj = cJSON_GetObjectItemCaseSensitive(p, "regenMs");

    if (!cJSON_IsNumber(ej) || !cJSON_IsNumber(tj)) {
        cJSON_AddStringToObject(r, "ok", "ESP_ERR_INVALID_ARG");
        return r;
    }
    transport_kline_kwp_cfg_t cfg = {
        .ecu_address    = (uint8_t)(ej->valueint & 0xFF),
        .tester_address = (uint8_t)(tj->valueint & 0xFF),
        .baud_rate      = (cJSON_IsNumber(bj)  && bj->valueint  > 0) ? (uint32_t)bj->valueint  : 10400,
        .timeout_p2_ms  = (cJSON_IsNumber(tmj) && tmj->valueint > 0) ? (uint32_t)tmj->valueint : 1200,
        .timeout_p1_ms  = (cJSON_IsNumber(tej) && tej->valueint > 0) ? (uint32_t)tej->valueint : 50,
        .regen_p3_ms    = (cJSON_IsNumber(rgj) && rgj->valueint > 0) ? (uint32_t)rgj->valueint : 20,
    };

    uint8_t payload[256];
    size_t payload_len = decode_bytes(pj, payload, sizeof(payload));

    // Reconstruct the on-wire TX for diagnostics so the response shows
    // exactly what the wrapping protocol layer put on the K-line.
    uint8_t tx_disp[256 + 5];
    size_t tx_disp_len = kline_build_bmw_fast_telegram(payload, payload_len,
                                                       cfg.ecu_address, cfg.tester_address,
                                                       tx_disp, sizeof(tx_disp));
    cJSON_AddItemToObject(r, "tx", bytes_to_json_array(tx_disp, tx_disp_len));
    char hex[3 * 256 + 1] = { 0 };
    encode_hex(tx_disp, tx_disp_len, hex, sizeof(hex));
    cJSON_AddStringToObject(r, "txHex", hex);

    uint8_t rx[256] = { 0 };
    size_t rx_len = 0;
    esp_err_t err = transport_kline_kwp_transact(&cfg, payload, payload_len,
                                                  rx, sizeof(rx), &rx_len);
    cJSON_AddItemToObject(r, "rx", bytes_to_json_array(rx, rx_len));
    encode_hex(rx, rx_len, hex, sizeof(hex));
    cJSON_AddStringToObject(r, "rxHex", hex);

    bool ok = (err == ESP_OK);
    cJSON_AddBoolToObject(r, "checksumOk", ok);
    if (ok) {
        size_t off = 0, len = 0;
        if (kline_bmw_fast_payload_window(rx, rx_len, &off, &len)) {
            cJSON_AddItemToObject(r, "payload", bytes_to_json_array(rx + off, len));
            encode_hex(rx + off, len, hex, sizeof(hex));
            cJSON_AddStringToObject(r, "payloadHex", hex);
        }
    }
    cJSON_AddStringToObject(r, "ok", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

// `klineDs2Request { concept, payload, timeoutMs?, telEndMs?, regenMs?, interByteMs?, baud?, checksumByUser? }`
//
// Thin wrapper around transport_kline_ds2_transact (the canonical TS-port
// of Ds2Session.sendRequest). Returns the full received telegram in `rx`
// plus a `checksumOk` flag and the on-wire `txHex` for diagnostics.
static cJSON *handle_klineDs2Request(const cJSON *p) {
    cJSON *r = cJSON_CreateObject();

    const cJSON *cj  = cJSON_GetObjectItemCaseSensitive(p, "concept");
    const cJSON *pj  = cJSON_GetObjectItemCaseSensitive(p, "payload");
    const cJSON *bj  = cJSON_GetObjectItemCaseSensitive(p, "baud");
    const cJSON *tmj = cJSON_GetObjectItemCaseSensitive(p, "timeoutMs");
    const cJSON *tej = cJSON_GetObjectItemCaseSensitive(p, "telEndMs");
    const cJSON *rgj = cJSON_GetObjectItemCaseSensitive(p, "regenMs");
    const cJSON *ibj = cJSON_GetObjectItemCaseSensitive(p, "interByteMs");
    const cJSON *uxj = cJSON_GetObjectItemCaseSensitive(p, "checksumByUser");

    if (!cJSON_IsNumber(cj)) {
        cJSON_AddStringToObject(r, "ok", "ESP_ERR_INVALID_ARG (concept)");
        return r;
    }

    transport_kline_ds2_cfg_t cfg = {
        .concept           = (uint16_t)cj->valueint,
        .baud_rate         = (cJSON_IsNumber(bj)  && bj->valueint  > 0) ? (uint32_t)bj->valueint  : 9600,
        .timeout_std_ms    = (cJSON_IsNumber(tmj) && tmj->valueint > 0) ? (uint32_t)tmj->valueint : 1200,
        .tel_end_ms        = (cJSON_IsNumber(tej) && tej->valueint > 0) ? (uint32_t)tej->valueint : 50,
        .regen_time_ms     = (cJSON_IsNumber(rgj) && rgj->valueint > 0) ? (uint32_t)rgj->valueint : 0,
        .inter_byte_ms     = (cJSON_IsNumber(ibj) && ibj->valueint > 0) ? (uint32_t)ibj->valueint : 0,
        .checksum_by_user  = cJSON_IsBool(uxj) && cJSON_IsTrue(uxj),
        .checksum_no_check = false,
    };

    uint8_t payload[256];
    size_t payload_len = decode_bytes(pj, payload, sizeof(payload));
    if (payload_len == 0) {
        cJSON_AddStringToObject(r, "ok", "ESP_ERR_INVALID_ARG (payload)");
        return r;
    }

    // Reconstruct on-wire TX (payload + optional XOR) for the diagnostic
    // echo. Mirrors what transact will actually write.
    uint8_t tx_disp[260];
    size_t tx_disp_len = payload_len;
    memcpy(tx_disp, payload, payload_len);
    if (!cfg.checksum_by_user) {
        tx_disp[tx_disp_len++] = kline_xor_checksum(tx_disp, 0, payload_len);
    }
    cJSON_AddItemToObject(r, "tx", bytes_to_json_array(tx_disp, tx_disp_len));
    char hex[3 * 256 + 1] = { 0 };
    encode_hex(tx_disp, tx_disp_len, hex, sizeof(hex));
    cJSON_AddStringToObject(r, "txHex", hex);

    uint8_t rx[256] = { 0 };
    size_t rx_len = 0;
    esp_err_t err = transport_kline_ds2_transact(&cfg, payload, payload_len,
                                                  rx, sizeof(rx), &rx_len);

    cJSON_AddItemToObject(r, "rx", bytes_to_json_array(rx, rx_len));
    encode_hex(rx, rx_len, hex, sizeof(hex));
    cJSON_AddStringToObject(r, "rxHex", hex);
    cJSON_AddBoolToObject(r, "checksumOk", err == ESP_OK);
    cJSON_AddStringToObject(r, "ok", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return r;
}

static const jsonrpc_method_t k_methods[] = {
    // Session lifecycle
    { "init",            handle_init           },
    { "end",             handle_end            },

    // Introspection
    { "info",            handle_info           },
    { "state",           handle_state         },
    { "errorCode",       handle_errorCode     },
    { "errorText",       handle_errorText     },
    { "listSgbd",        handle_listSgbd      },
    { "listJobs",        handle_listJobs      },
    { "getJobMetadata",  handle_getJobMetadata },
    { "disassembleJob",  handle_disassembleJob },

    // Job execution + result accessors (stubs until transport lands)
    { "job",             handle_job           },
    { "resultSets",      handle_resultSets    },
    { "resultText",      handle_resultText    },
    { "resultInt",       handle_resultInt     },
    { "resultReal",      handle_resultReal    },
    { "resultBinary",    handle_resultBinary  },
    { "resultFormat",    handle_resultFormat  },
    { "break",           handle_break         },

    // Log streaming (subscription is accepted but no broadcasts yet)
    { "log.subscribe",   handle_logSubscribe  },
    { "log.unsubscribe", handle_logUnsubscribe },

    // Bench debug — direct K-line UART probe; bypasses the VM
    { "klineProbe",        handle_klineProbe      },
    { "klineWireTest",     handle_klineWireTest   },
    { "klineHoldTx",       handle_klineHoldTx     },

    // K-line protocol — ported from ediabasx TS reference
    { "klineSetBaud",      handle_klineSetBaud    },
    { "klineSetParity",    handle_klineSetParity  },
    { "klineSlowInit",     handle_klineSlowInit   },
    { "klineFastInit",     handle_klineFastInit   },
    { "klineKwpRequest",   handle_klineKwpRequest },
    { "klineDs2Request",   handle_klineDs2Request },
};

static const jsonrpc_method_t *find_method(const char *name) {
    for (size_t i = 0; i < sizeof(k_methods) / sizeof(k_methods[0]); ++i) {
        if (strcmp(k_methods[i].method, name) == 0) {
            return &k_methods[i];
        }
    }
    return NULL;
}

// ---- envelope helpers -----------------------------------------------------

// Returns an owned, allocated JSON string. Caller free()s.
static char *make_error(const cJSON *id, int code, const char *message) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    } else {
        cJSON_AddNullToObject(root, "id");
    }
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message);
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static char *make_result(const cJSON *id, cJSON *result_owned) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, true));
    } else {
        cJSON_AddNullToObject(root, "id");
    }
    cJSON_AddItemToObject(root, "result", result_owned);  // takes ownership
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

// ---- message dispatch -----------------------------------------------------

// Returns an allocated response string to send back over the WS (NULL
// if the request was a notification with no response expected).
static char *dispatch_message(const char *msg, size_t len) {
    cJSON *root = cJSON_ParseWithLength(msg, len);
    if (!root) {
        return make_error(NULL, -32700, "Parse error");
    }

    const cJSON *id     = cJSON_GetObjectItemCaseSensitive(root, "id");
    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    if (!cJSON_IsString(method) || method->valuestring == NULL) {
        char *resp = make_error(id, -32600, "Invalid Request");
        cJSON_Delete(root);
        return resp;
    }

    const jsonrpc_method_t *m = find_method(method->valuestring);
    if (!m) {
        char *resp = make_error(id, -32601, "Method not found");
        cJSON_Delete(root);
        return resp;
    }

    cJSON *result = m->handler(params);
    char *resp = id ? make_result(id, result) : NULL;
    if (!id) cJSON_Delete(result);   // notification — no response

    cJSON_Delete(root);
    return resp;
}

// ---- WebSocket handler ----------------------------------------------------

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // esp_http_server completes the WS handshake on its own when
        // `.is_websocket = true`. Nothing to do here on the first call.
        ESP_LOGI(TAG, "WS connection opened");
        return ESP_OK;
    }

    httpd_ws_frame_t frame = { 0 };
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ws_recv_frame(len): %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGD(TAG, "WS frame type=%d len=%u", (int)frame.type, (unsigned)frame.len);

    // Only TEXT frames carry JSON-RPC. PING/PONG/CLOSE are auto-handled
    // by esp_http_server's control-frame plumbing.
    if (frame.type != HTTPD_WS_TYPE_TEXT) {
        return ESP_OK;
    }
    if (frame.len == 0) {
        return ESP_OK;
    }

    uint8_t *buf = malloc(frame.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ws_recv_frame(payload): %s", esp_err_to_name(err));
        free(buf);
        return err;
    }
    buf[frame.len] = '\0';
    ESP_LOGI(TAG, "WS rx %u bytes: %.*s",
             (unsigned)frame.len,
             frame.len > 120 ? 120 : (int)frame.len, (const char *)buf);

    char *response = dispatch_message((const char *)buf, frame.len);
    free(buf);

    if (response) {
        size_t rlen = strlen(response);
        ESP_LOGI(TAG, "WS tx %u bytes: %.*s",
                 (unsigned)rlen,
                 rlen > 200 ? 200 : (int)rlen, response);
        httpd_ws_frame_t out = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)response,
            .len     = rlen,
        };
        err = httpd_ws_send_frame(req, &out);
        free(response);
    }
    return err;
}

esp_err_t jsonrpc_start(void) {
    httpd_handle_t server = http_static_handle();
    if (!server) {
        ESP_LOGE(TAG, "http_static not started — call http_static_start() first");
        return ESP_FAIL;
    }

    // All JSON-RPC endpoints share a /rpc/ prefix so they can't collide
    // with the /<app>/ static-app fallback. Future sibling RPCs land at
    // /rpc/j2534, /rpc/nfsx, etc.
    httpd_uri_t rpc = {
        .uri          = "/rpc/ediabasx",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    esp_err_t err = httpd_register_uri_handler(server, &rpc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register /rpc/ediabasx failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "WS endpoint up at /rpc/ediabasx (3 methods: info, state, errorText)");
    return ESP_OK;
}
