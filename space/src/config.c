#include "../include/smart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ─── Defaults ──────────────────────────────────────────────────── */
void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    strncpy(cfg->asset,    "USD_CAD",  sizeof cfg->asset - 1);
    strncpy(cfg->log_path, "logs/smart.log", sizeof cfg->log_path - 1);
    cfg->practice       = true;
    cfg->paper          = true;
    cfg->capital        = 10000.0;
    cfg->risk_pct       = 1.0;
    cfg->entropy_thresh = ENTROPY_THRESH;
    cfg->min_confidence = MIN_CONFIDENCE;
    cfg->min_bias       = MIN_BIAS;
    cfg->max_energy     = ENERGY_THRESH;
    cfg->curl_thresh    = CURL_THRESH;
    cfg->min_delta      = MIN_DELTA;
    cfg->max_daily_loss_pct = 5.0;
    cfg->log_level      = 1;   /* INFO */
    cfg->asset_bias     = 0;   /* both directions */
}

/* ─── Minimal JSON Key-Value Parser ─────────────────────────────── */
/*
 * Not a full JSON parser.  Handles flat objects only: 
 *   "key": value   or   "key": "string"
 * Sufficient for our config file format.
 */
static const char *json_find_key(const char *buf, const char *key) {
    char needle[128];
    snprintf(needle, sizeof needle, "\"%s\"", key);
    const char *p = strstr(buf, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == ' ') p++;
    return p;
}

static bool json_str(const char *buf, const char *key, char *out, int maxlen) {
    const char *p = json_find_key(buf, key);
    if (!p || *p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxlen - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool json_double(const char *buf, const char *key, double *out) {
    const char *p = json_find_key(buf, key);
    if (!p) return false;
    char *end;
    double v = strtod(p, &end);
    if (end == p) return false;
    *out = v;
    return true;
}

static bool json_bool(const char *buf, const char *key, bool *out) {
    const char *p = json_find_key(buf, key);
    if (!p) return false;
    if (strncmp(p, "true",  4) == 0) { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool json_int(const char *buf, const char *key, int *out) {
    double v;
    if (!json_double(buf, key, &v)) return false;
    *out = (int)v;
    return true;
}

/* ─── Loader ────────────────────────────────────────────────────── */
bool config_load(const char *path, Config *cfg) {
    config_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) return false;   /* not fatal – use defaults */

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return false; }
    buf[sz] = '\0';
    fclose(f);

    /* Parse fields */
    json_str   (buf, "asset",          cfg->asset,      sizeof cfg->asset);
    json_str   (buf, "api_key",        cfg->api_key,    sizeof cfg->api_key);
    json_str   (buf, "account_id",     cfg->account_id, sizeof cfg->account_id);
    json_str   (buf, "log_path",       cfg->log_path,   sizeof cfg->log_path);
    json_bool  (buf, "practice",       &cfg->practice);
    json_bool  (buf, "paper",          &cfg->paper);
    json_double(buf, "capital",        &cfg->capital);
    json_double(buf, "risk_pct",       &cfg->risk_pct);
    json_double(buf, "entropy_thresh", &cfg->entropy_thresh);
    json_double(buf, "min_confidence", &cfg->min_confidence);
    json_double(buf, "min_bias",       &cfg->min_bias);
    json_double(buf, "max_energy",     &cfg->max_energy);
    json_double(buf, "curl_thresh",    &cfg->curl_thresh);
    json_double(buf, "min_delta",      &cfg->min_delta);
    json_double(buf, "max_daily_loss", &cfg->max_daily_loss_pct);
    json_int   (buf, "log_level",      &cfg->log_level);
    json_int   (buf, "asset_bias",     &cfg->asset_bias);

    free(buf);
    return true;
}
