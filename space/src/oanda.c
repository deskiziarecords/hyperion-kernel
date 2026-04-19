/*
 * oanda.c  –  OANDA v20 REST interface
 *
 * In PAPER / BACKTEST mode all functions are stubs that return
 * synthetic data so the full trading loop can be exercised without
 * a live API key.
 *
 * In LIVE mode (--live) the HTTP client calls OANDA's practice or
 * production endpoints.  HTTP is implemented via POSIX sockets +
 * TLS via mbedTLS (link with -lmbedtls -lmbedcrypto -lmbedx509).
 * For now the live path is stubbed; see oanda_live.c (TODO).
 */

#define _POSIX_C_SOURCE 200809L
#include "../include/smart.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

/* ─── Internal State ────────────────────────────────────────────── */
static bool     g_connected = false;
static bool     g_paper     = true;

/* Synthetic price state for paper trading */
static double   g_synth_price = 1.34500;
static uint64_t g_synth_ts    = 0;

/* ─── Helpers ───────────────────────────────────────────────────── */
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

/* Simple LCG random in [0,1) */
static double rand01(void) {
    return rand() / (double)RAND_MAX;
}

/* ─── Connection ────────────────────────────────────────────────── */
bool oanda_connect(const Config *cfg) {
    g_paper     = cfg->paper || !cfg->practice;
    g_synth_ts  = now_ms();
    g_connected = true;

    if (g_paper)
        log_info("[OANDA] PAPER mode – using synthetic price feed");
    else
        log_info("[OANDA] Connecting to OANDA v20 (practice=%s)",
                 cfg->practice ? "yes" : "NO - LIVE MONEY");

    return true;
}

void oanda_disconnect(void) {
    g_connected = false;
    log_info("[OANDA] Disconnected");
}

/* ─── Price Feed ────────────────────────────────────────────────── */
/*
 * oanda_fetch_candle()
 *
 * In paper mode: generates a synthetic 1-minute candle using a
 * small random walk around g_synth_price.  Spreads are fixed at
 * 0.0002 (2 pips).
 *
 * In live mode: TODO – poll OANDA /v3/instruments/{inst}/candles
 */
bool oanda_fetch_candle(const Config *cfg, Candle *out) {
    (void)cfg;
    if (!g_connected) return false;

    if (g_paper) {
        /* Random walk: drift ±0.05%, range 0.05–0.25% */
        double drift  = (rand01() - 0.50) * 0.0010;
        double hi_ext = rand01() * 0.0015;
        double lo_ext = rand01() * 0.0015;

        double o = g_synth_price;
        double c = o * (1.0 + drift);
        double h = fmax(o, c) * (1.0 + hi_ext);
        double l = fmin(o, c) * (1.0 - lo_ext);

        out->open         = o;
        out->high         = h;
        out->low          = l;
        out->close        = c;
        out->volume       = (uint64_t)(rand01() * 5000 + 500);
        out->spread       = 0.0002;
        out->timestamp_ms = g_synth_ts;

        g_synth_price = c;
        g_synth_ts   += 60000;   /* +1 minute */
        return true;
    }

    /* LIVE stub – not yet implemented */
    log_info("[OANDA] LIVE candle fetch not yet implemented");
    return false;
}

bool oanda_get_price(const Config *cfg, double *bid, double *ask) {
    (void)cfg;
    *bid = g_synth_price - 0.0001;
    *ask = g_synth_price + 0.0001;
    return g_connected;
}

/* ─── Order Execution ───────────────────────────────────────────── */
bool oanda_place_order(const Config *cfg, const Signal *sig,
                       double entry_price, OpenPosition *pos_out) {
    if (!g_connected) return false;

    double sl_pct = SYM_SL_PCT[0];   /* default B-pattern SL */
    double sl_dist = entry_price * sl_pct;
    double tp_dist = sl_dist * 2.0;  /* 2:1 RR */

    pos_out->timestamp_ms = now_ms();
    pos_out->direction    = sig->direction;
    pos_out->entry_price  = entry_price;
    pos_out->lot_size     = sig->lot_size;
    pos_out->signal       = *sig;

    if (sig->direction == 1) {  /* LONG */
        pos_out->sl_price = entry_price - sl_dist;
        pos_out->tp_price = entry_price + tp_dist;
    } else {                    /* SHORT */
        pos_out->sl_price = entry_price + sl_dist;
        pos_out->tp_price = entry_price - tp_dist;
    }

    log_info("[ORDER] %s %.2f lots @ %.5f  SL=%.5f  TP=%.5f  [%s]",
             sig->direction == 1 ? "LONG" : "SHORT",
             pos_out->lot_size,
             entry_price,
             pos_out->sl_price, pos_out->tp_price,
             cfg->paper ? "PAPER" : "LIVE");

    return true;
}

void oanda_close_position(const Config *cfg, const OpenPosition *pos,
                          double current_price, double *pnl_out) {
    (void)cfg;
    double raw_pnl = (current_price - pos->entry_price) * pos->direction;

    /* Convert price diff to pips (assume 4-decimal pair: 1 pip = 0.0001) */
    *pnl_out = raw_pnl / 0.0001;

    log_info("[CLOSE] PnL=%.1f pips  exit=%.5f", *pnl_out, current_price);
}
