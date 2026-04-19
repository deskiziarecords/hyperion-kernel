#include "../include/smart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * backtest_run()
 *
 * Reads a CSV file with columns:
 *   timestamp_ms,open,high,low,close,volume
 * (header line optional – detected by non-numeric first field)
 *
 * Replays candle-by-candle through the full signal pipeline.
 * Simulates a single-position model: one trade at a time.
 *
 * Returns number of trades taken.
 */
int backtest_run(const char *csv_path, const Config *cfg) {
    FILE *f = fopen(csv_path, "r");
    if (!f) {
        log_info("[BACKTEST] Cannot open: %s", csv_path);
        return -1;
    }

    log_info("[BACKTEST] Starting replay: %s", csv_path);

    SeqBuffer sb = {0};
    Symbol    seq[SEQ_LEN];

    /* Running stats */
    int    total_trades = 0;
    int    wins         = 0;
    int    losses       = 0;
    double total_pnl    = 0.0;
    double max_dd       = 0.0;
    double peak_pnl     = 0.0;
    double win_sum      = 0.0;
    double loss_sum     = 0.0;

    /* Open position state */
    bool          in_trade = false;
    OpenPosition  cur_pos  = {0};

    char line[512];
    int  line_no = 0;

    while (fgets(line, sizeof line, f)) {
        line_no++;
        /* Strip newline */
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0]) continue;

        /* Skip header if first char is not digit */
        if (line_no == 1 && (line[0] < '0' || line[0] > '9')) continue;

        /* Parse CSV: ts,o,h,l,c[,volume] */
        Candle c = {0};
        char *tok = strtok(line, ",");
        if (!tok) continue;
        c.timestamp_ms = (uint64_t)strtoull(tok, NULL, 10);

        tok = strtok(NULL, ","); if (!tok) continue; c.open  = atof(tok);
        tok = strtok(NULL, ","); if (!tok) continue; c.high  = atof(tok);
        tok = strtok(NULL, ","); if (!tok) continue; c.low   = atof(tok);
        tok = strtok(NULL, ","); if (!tok) continue; c.close = atof(tok);
        tok = strtok(NULL, ",");
        c.volume = tok ? (uint64_t)strtoull(tok, NULL, 10) : 0;

        /* ── Check open position against current candle ── */
        if (in_trade) {
            double pnl = 0.0;
            bool   closed = false;
            const char *reason = "tp";

            if (cur_pos.direction == 1) {
                if (c.low <= cur_pos.sl_price) {
                    pnl    = (cur_pos.sl_price - cur_pos.entry_price) / 0.0001;
                    reason = "sl";  closed = true;
                } else if (c.high >= cur_pos.tp_price) {
                    pnl    = (cur_pos.tp_price - cur_pos.entry_price) / 0.0001;
                    reason = "tp";  closed = true;
                }
            } else {
                if (c.high >= cur_pos.sl_price) {
                    pnl    = (cur_pos.entry_price - cur_pos.sl_price) / 0.0001;
                    reason = "sl";  closed = true;
                } else if (c.low <= cur_pos.tp_price) {
                    pnl    = (cur_pos.entry_price - cur_pos.tp_price) / 0.0001;
                    reason = "tp";  closed = true;
                }
            }

            if (closed) {
                total_pnl += pnl;
                if (pnl > 0) { wins++;   win_sum  += pnl; }
                else         { losses++; loss_sum += -pnl; }

                if (total_pnl > peak_pnl) peak_pnl = total_pnl;
                double dd = peak_pnl - total_pnl;
                if (dd > max_dd) max_dd = dd;

                memory_store(cur_pos.entry_seq, pnl);
                log_debug("[BT] CLOSE %s pnl=%.1f pips  reason=%s",
                          cur_pos.direction==1?"LONG":"SHORT", pnl, reason);

                in_trade = false;
            }
        }

        /* ── Encode candle and update sequence ── */
        Symbol sym = encode_candle(&c);
        seqbuf_push(&sb, sym);
        seqbuf_read(&sb, seq);

        if (in_trade) continue;   /* only one position at a time */

        /* ── Evaluate signal ── */
        Signal sig = evaluate_signal(seq, cfg);

        if (!sig.valid) {
            char blk_buf[SEQ_LEN+1];
            log_blocked(&sig, seq_to_str(seq, blk_buf));
            continue;
        }

        /* ── Size position ── */
        double wr   = (total_trades > 0) ? (double)wins / total_trades : 0.55;
        double aw   = (wins   > 0) ? win_sum  / wins   : 12.0;
        double al   = (losses > 0) ? loss_sum / losses :  8.0;
        sig.lot_size = kelly_size(wr, aw, al, sig.confidence, sig.energy, cfg);

        /* ── Open position ── */
        double entry = (c.open + c.close) / 2.0;   /* mid of bar */
        double sl_d  = entry * SYM_SL_PCT[0];
        double tp_d  = sl_d * 2.0;

        cur_pos.timestamp_ms = c.timestamp_ms;
        cur_pos.direction    = sig.direction;
        cur_pos.entry_price  = entry;
        cur_pos.lot_size     = sig.lot_size;
        cur_pos.signal       = sig;
        memcpy(cur_pos.entry_seq, seq, SEQ_LEN * sizeof(Symbol));
        cur_pos.sl_price = (sig.direction == 1) ? entry - sl_d : entry + sl_d;
        cur_pos.tp_price = (sig.direction == 1) ? entry + tp_d : entry - tp_d;

        in_trade = true;
        total_trades++;

        log_debug("[BT] OPEN %s %.2f lots @ %.5f  conf=%.2f  ent=%.2f",
                  sig.direction==1?"LONG":"SHORT", sig.lot_size, entry,
                  sig.confidence, sig.entropy);
    }

    fclose(f);

    /* ── Summary ── */
    double wr = total_trades > 0 ? 100.0 * wins / total_trades : 0.0;
    log_info("─────────────────────────────────────────");
    log_info("[BACKTEST] Complete: %d candles processed", line_no);
    log_info("[BACKTEST] Trades: %d  |  Wins: %d (%.1f%%)  |  Losses: %d",
             total_trades, wins, wr, losses);
    log_info("[BACKTEST] Total PnL: %.1f pips  |  Max DD: %.1f pips", total_pnl, max_dd);
    log_info("[BACKTEST] Avg win: %.1f  |  Avg loss: %.1f",
             wins > 0 ? win_sum / wins : 0.0,
             losses > 0 ? loss_sum / losses : 0.0);
    log_info("─────────────────────────────────────────");

    return total_trades;
}
