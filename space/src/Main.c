#include "../include/smart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

/* ─── Global State ──────────────────────────────────────────────── */
static volatile bool g_running  = true;
static bool          g_in_trade = false;
static OpenPosition  g_pos      = {0};

/* Running performance counters */
static int    g_total_trades = 0;
static int    g_wins         = 0;
static int    g_losses       = 0;
static double g_total_pnl    = 0.0;
static double g_win_sum      = 0.0;
static double g_loss_sum     = 0.0;
static double g_daily_pnl    = 0.0;

/* ─── Signal Handler ────────────────────────────────────────────── */
static void handle_signal(int sig) {
    (void)sig;
    g_running = false;
    fprintf(stderr, "\n[SMART-EXE] Shutdown requested...\n");
}

/* USR1 = emergency flatten */
static void handle_usr1(int sig) {
    (void)sig;
    fprintf(stderr, "[SMART-EXE] KILL SWITCH (USR1) – flatten all positions\n");
    g_in_trade = false;
    g_running  = false;
}

/* ─── CLI Parsing ───────────────────────────────────────────────── */
typedef struct {
    bool   backtest;
    char   backtest_csv[256];
    bool   paper;
    bool   live;
    bool   once;
    char   once_seq[32];
    bool   daemon_mode;
    bool   verbose;
    char   config_path[256];
    char   import_mem[256];
    char   export_mem[256];
} Args;

static void print_usage(const char *argv0) {
    fprintf(stderr,
        "SMART-EXE v1.0 – Single-Asset Trading Executable\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "Modes:\n"
        "  --paper                 Paper trading (default, no real orders)\n"
        "  --live                  Live trading (requires --api-key in config)\n"
        "  --backtest <csv>        Replay historical CSV data\n"
        "  --once --sequence <s>   Evaluate a single 20-symbol sequence\n"
        "  --daemon                Run in background (no interactive output)\n"
        "\n"
        "Options:\n"
        "  --config <path>         Config JSON (default: config.json)\n"
        "  --import-memory <f>     Load FAISS memory from file\n"
        "  --export-memory <f>     Save FAISS memory to file on exit\n"
        "  --verbose               Set log level to DEBUG\n"
        "  --help                  Show this message\n",
        argv0);
}

static void parse_args(int argc, char **argv, Args *a) {
    memset(a, 0, sizeof *a);
    strncpy(a->config_path, "config.json", sizeof a->config_path - 1);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--paper"))    { a->paper   = true; continue; }
        if (!strcmp(argv[i], "--live"))     { a->live    = true; continue; }
        if (!strcmp(argv[i], "--daemon"))   { a->daemon_mode = true; continue; }
        if (!strcmp(argv[i], "--verbose"))  { a->verbose = true; continue; }
        if (!strcmp(argv[i], "--help"))     { print_usage(argv[0]); exit(0); }
        if (!strcmp(argv[i], "--once"))     { a->once    = true; continue; }

        if (!strcmp(argv[i], "--backtest") && i+1 < argc) {
            a->backtest = true;
            strncpy(a->backtest_csv, argv[++i], sizeof a->backtest_csv - 1);
            continue;
        }
        if (!strcmp(argv[i], "--sequence") && i+1 < argc) {
            strncpy(a->once_seq, argv[++i], sizeof a->once_seq - 1);
            continue;
        }
        if (!strcmp(argv[i], "--config") && i+1 < argc) {
            strncpy(a->config_path, argv[++i], sizeof a->config_path - 1);
            continue;
        }
        if (!strcmp(argv[i], "--import-memory") && i+1 < argc) {
            strncpy(a->import_mem, argv[++i], sizeof a->import_mem - 1);
            continue;
        }
        if (!strcmp(argv[i], "--export-memory") && i+1 < argc) {
            strncpy(a->export_mem, argv[++i], sizeof a->export_mem - 1);
            continue;
        }
        fprintf(stderr, "[WARN] Unknown arg: %s\n", argv[i]);
    }

    if (!a->paper && !a->live) a->paper = true;   /* default */
}

/* ─── Once Mode ─────────────────────────────────────────────────── */
static void run_once(const char *seq_str, const Config *cfg) {
    int len = (int)strlen(seq_str);
    if (len < 1) { fprintf(stderr, "Empty sequence\n"); return; }

    /* Parse sequence string → symbols */
    Symbol seq[SEQ_LEN] = {0};
    SeqBuffer sb = {0};
    for (int i = 0; i < len && i < SEQ_LEN; i++) {
        Symbol s = SYM_X;
        switch (seq_str[i]) {
            case 'B': s = SYM_B; break; case 'I': s = SYM_I; break;
            case 'W': s = SYM_W; break; case 'w': s = SYM_w; break;
            case 'U': s = SYM_U; break; case 'D': s = SYM_D; break;
        }
        seqbuf_push(&sb, s);
    }
    seqbuf_read(&sb, seq);

    char sbuf[SEQ_LEN+1];
    printf("[SMART-EXE] Sequence : %s\n", seq_to_str(seq, sbuf));

    Signal sig = evaluate_signal(seq, cfg);
    char dir[8];
    if (!sig.valid) {
        snprintf(dir, sizeof dir, "BLOCK");
        printf("[SMART-EXE] Decision : BLOCKED (gate λ%d)\n", sig.block_gate);
    } else {
        snprintf(dir, sizeof dir, sig.direction == 1 ? "LONG" : "SHORT");
        printf("[SMART-EXE] Decision : %s\n", dir);
    }
    printf("[SMART-EXE] Entropy  : %.3f  (thresh %.2f) %s\n",
           sig.entropy, cfg->entropy_thresh, sig.entropy < cfg->entropy_thresh ? "✓" : "✗");
    printf("[SMART-EXE] Bias     : %.3f  (min %.2f)   %s\n",
           sig.bias, cfg->min_bias, sig.bias > cfg->min_bias ? "✓" : "✗");
    printf("[SMART-EXE] Conf     : %.3f  (min %.2f)   %s\n",
           sig.confidence, cfg->min_confidence, sig.confidence >= cfg->min_confidence ? "✓" : "✗");
    printf("[SMART-EXE] Energy   : %.3f  (max %.2f)   %s\n",
           sig.energy, cfg->max_energy, sig.energy < cfg->max_energy ? "✓" : "✗");
    printf("[SMART-EXE] Curl     : %.3f  (max %.2f)   %s\n",
           sig.curl, cfg->curl_thresh, sig.curl <= cfg->curl_thresh ? "✓" : "✗");
    printf("[SMART-EXE] Delta    : %.0f  (min %.0f)   %s\n",
           sig.delta, cfg->min_delta, fabs(sig.delta) >= cfg->min_delta ? "✓" : "✗");
}

/* ─── Main Trading Loop ──────────────────────────────────────────── */
static void run_loop(const Config *cfg) {
    SeqBuffer sb = {0};
    Symbol    seq[SEQ_LEN];

    log_info("[SMART-EXE] %s Sentinel – %s mode",
             cfg->asset, cfg->paper ? "PAPER" : "LIVE");
    log_info("[SMART-EXE] Memory: %d patterns loaded", memory_count());

    if (!oanda_connect(cfg)) {
        log_info("[ERROR] Cannot connect to data feed");
        return;
    }

    while (g_running) {
        /* ── Fetch new candle ── */
        Candle c = {0};
        if (!oanda_fetch_candle(cfg, &c)) {
            log_info("[WARN] Feed error – retrying in 5s");
            sleep(5);
            continue;
        }

        Symbol sym = encode_candle(&c);
        seqbuf_push(&sb, sym);
        seqbuf_read(&sb, seq);

        char sbuf[SEQ_LEN + 1];
        log_debug("[CANDLE] %s → %c", seq_to_str(seq, sbuf), SYM_CHAR[(int)sym]);

        /* ── Monitor open position ── */
        if (g_in_trade) {
            double bid, ask;
            oanda_get_price(cfg, &bid, &ask);
            double current = (g_pos.direction == 1) ? bid : ask;

            bool sl_hit = (g_pos.direction == 1) ? (current <= g_pos.sl_price)
                                                  : (current >= g_pos.sl_price);
            bool tp_hit = (g_pos.direction == 1) ? (current >= g_pos.tp_price)
                                                  : (current <= g_pos.tp_price);

            if (sl_hit || tp_hit) {
                double pnl = 0.0;
                oanda_close_position(cfg, &g_pos, current, &pnl);
                memory_store(g_pos.entry_seq, pnl);
                log_trade(&g_pos, pnl, sl_hit ? "sl" : "tp");

                g_total_pnl  += pnl;
                g_daily_pnl  += pnl;
                g_total_trades++;
                if (pnl > 0) { g_wins++;   g_win_sum  += pnl; }
                else         { g_losses++; g_loss_sum += -pnl; }

                g_in_trade = false;

                /* Daily loss circuit breaker */
                if (g_daily_pnl < -(cfg->max_daily_loss_pct / 100.0 * cfg->capital / 0.0001)) {
                    log_info("[RISK] Daily loss limit hit – stopping for today");
                    break;
                }
            }
        }

        if (g_in_trade) {
            /* Waiting for position to close – skip new signal eval */
            sleep(60);
            continue;
        }

        /* ── Evaluate signal ── */
        Signal sig = evaluate_signal(seq, cfg);

        if (!sig.valid) {
            log_debug("[BLOCK] λ%d | ent=%.2f bias=%.2f conf=%.2f",
                      sig.block_gate, sig.entropy, sig.bias, sig.confidence);
            log_blocked(&sig, seq_to_str(seq, sbuf));
            sleep(60);
            continue;
        }

        /* ── Size position ── */
        double wr = g_total_trades > 0 ? (double)g_wins / g_total_trades : 0.55;
        double aw = g_wins   > 0 ? g_win_sum  / g_wins   : 12.0;
        double al = g_losses > 0 ? g_loss_sum / g_losses :  8.0;
        sig.lot_size = kelly_size(wr, aw, al, sig.confidence, sig.energy, cfg);

        /* ── λ6 micro-sanity check ── */
        double max_lots = (cfg->capital * MAX_POS_PCT) / (c.close * 100000.0);
        if (sig.lot_size > max_lots) sig.lot_size = max_lots;
        /* Round to 0.01 lot increments */
        sig.lot_size = round(sig.lot_size * 100.0) / 100.0;
        if (sig.lot_size < 0.01) { sleep(60); continue; }

        /* ── Execute ── */
        double entry = (g_pos.direction == 1) ? c.close + c.spread * 0.5
                                              : c.close - c.spread * 0.5;
        if (!oanda_place_order(cfg, &sig, c.close, &g_pos)) {
            log_info("[ERROR] Order placement failed");
            sleep(60);
            continue;
        }
        memcpy(g_pos.entry_seq, seq, SEQ_LEN * sizeof(Symbol));
        (void)entry;  /* entry_price set inside oanda_place_order */
        g_in_trade = true;

        log_info("[TRADE] %s %.2f lots | conf=%.2f ent=%.2f energy=%.2f",
                 sig.direction == 1 ? "LONG" : "SHORT",
                 sig.lot_size, sig.confidence, sig.entropy, sig.energy);

        sleep(60);
    }

    /* ── Shutdown cleanup ── */
    if (g_in_trade) {
        double bid, ask, pnl;
        oanda_get_price(cfg, &bid, &ask);
        double exit_p = (g_pos.direction == 1) ? bid : ask;
        oanda_close_position(cfg, &g_pos, exit_p, &pnl);
        log_info("[SHUTDOWN] Flat. Final PnL on closed pos: %.1f pips", pnl);
    }

    oanda_disconnect();

    log_info("══════════════════════════════════════════");
    log_info("Session Summary | Trades: %d | Wins: %d | PnL: %.1f pips",
             g_total_trades, g_wins, g_total_pnl);
    log_info("══════════════════════════════════════════");
}

/* ─── Entry Point ───────────────────────────────────────────────── */
int main(int argc, char **argv) {
    Args args;
    parse_args(argc, argv, &args);

    Config cfg;
    config_load(args.config_path, &cfg);

    if (args.verbose)     cfg.log_level = 2;
    if (args.paper)       cfg.paper     = true;
    if (args.live)        cfg.paper     = false;

    log_init(&cfg);

    memory_init();
    if (args.import_mem[0])
        memory_load(args.import_mem);
    else
        memory_load("memory.bin");   /* auto-load default */

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);
#ifdef SIGUSR1
    signal(SIGUSR1, handle_usr1);
#endif

    if (args.once) {
        run_once(args.once_seq[0] ? args.once_seq : "BBUIBBXIBB", &cfg);
    } else if (args.backtest) {
        backtest_run(args.backtest_csv, &cfg);
    } else {
        run_loop(&cfg);
    }

    /* Save memory on exit */
    if (args.export_mem[0])
        memory_save(args.export_mem);
    else
        memory_save("memory.bin");

    log_close();
    return 0;
}
