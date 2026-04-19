#include "../include/smart.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>

static FILE *g_log_f     = NULL;
static FILE *g_trade_f   = NULL;
static FILE *g_blocked_f = NULL;
static int   g_level     = 1;

static void ensure_dir(const char *path) {
    /* Create leading directory if it doesn't exist */
    char tmp[256];
    strncpy(tmp, path, sizeof tmp - 1);
    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = '\0';
        mkdir(tmp, 0755);
    }
}

static void timestamp_str(char *buf, int len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", tm);
}

static void date_str(char *buf, int len) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(buf, len, "%Y-%m-%d", tm);
}

void log_init(const Config *cfg) {
    g_level = cfg->log_level;
    ensure_dir(cfg->log_path);

    g_log_f = fopen(cfg->log_path, "a");

    char date[32]; date_str(date, sizeof date);
    char trade_path[320], blocked_path[320];
    snprintf(trade_path,   sizeof trade_path,   "logs/trades_%s.jsonl",  date);
    snprintf(blocked_path, sizeof blocked_path, "logs/blocked_%s.jsonl", date);

    g_trade_f   = fopen(trade_path,   "a");
    g_blocked_f = fopen(blocked_path, "a");
}

void log_info(const char *fmt, ...) {
    if (g_level < 1) return;
    char ts[32]; timestamp_str(ts, sizeof ts);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    fprintf(stdout, "[%s] %s\n", ts, msg);
    if (g_log_f) { fprintf(g_log_f, "[%s] %s\n", ts, msg); fflush(g_log_f); }
}

void log_debug(const char *fmt, ...) {
    if (g_level < 2) return;
    char ts[32]; timestamp_str(ts, sizeof ts);

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    fprintf(stdout, "[%s] DBG %s\n", ts, msg);
    if (g_log_f) { fprintf(g_log_f, "[%s] DBG %s\n", ts, msg); fflush(g_log_f); }
}

void log_trade(const OpenPosition *pos, double pnl, const char *outcome) {
    if (!g_trade_f) return;
    char ts[32]; timestamp_str(ts, sizeof ts);
    char seq_str[SEQ_LEN + 1];
    seq_to_str(pos->entry_seq, seq_str);

    fprintf(g_trade_f,
        "{\"ts\":\"%s\",\"dir\":%d,\"entry\":%.5f,\"sl\":%.5f,\"tp\":%.5f,"
        "\"lots\":%.2f,\"pnl_pips\":%.1f,\"outcome\":\"%s\","
        "\"conf\":%.3f,\"entropy\":%.3f,\"energy\":%.3f,\"seq\":\"%s\"}\n",
        ts, pos->direction, pos->entry_price, pos->sl_price, pos->tp_price,
        pos->lot_size, pnl, outcome,
        pos->signal.confidence, pos->signal.entropy, pos->signal.energy,
        seq_str);
    fflush(g_trade_f);
}

void log_blocked(const Signal *sig, const char *seq_str) {
    if (!g_blocked_f) return;
    char ts[32]; timestamp_str(ts, sizeof ts);
    static const char *GATE_NAMES[] = {
        "NONE","ENTROPY","MEMORY_BIAS","CONFIDENCE","GEOMETRY","ASSET_BIAS","SANITY","MACRO"
    };
    int g = sig->block_gate;
    if (g < 0 || g > 7) g = 0;

    fprintf(g_blocked_f,
        "{\"ts\":\"%s\",\"gate\":%d,\"gate_name\":\"%s\","
        "\"entropy\":%.3f,\"bias\":%.3f,\"conf\":%.3f,"
        "\"energy\":%.3f,\"curl\":%.3f,\"seq\":\"%s\"}\n",
        ts, sig->block_gate, GATE_NAMES[g],
        sig->entropy, sig->bias, sig->confidence,
        sig->energy, sig->curl, seq_str);
    fflush(g_blocked_f);
}

void log_close(void) {
    if (g_log_f)     { fclose(g_log_f);     g_log_f     = NULL; }
    if (g_trade_f)   { fclose(g_trade_f);   g_trade_f   = NULL; }
    if (g_blocked_f) { fclose(g_blocked_f); g_blocked_f = NULL; }
}
