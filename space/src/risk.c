#include "../include/smart.h"
#include <math.h>

/*
 * evaluate_signal()
 *
 * Runs the full λ-gate cascade.
 * Returns a Signal; check sig.valid before trading.
 */
Signal evaluate_signal(const Symbol seq[SEQ_LEN], const Config *cfg) {
    Signal sig = {0};
    sig.valid      = false;
    sig.block_gate = 0;

    /* ── λ1: Entropy gate ─────────────────────────────────────── */
    sig.entropy = calc_entropy(seq);
    if (sig.entropy >= cfg->entropy_thresh) {
        sig.block_gate = 1;
        return sig;
    }

    /* ── λ2: Memory bias gate ──────────────────────────────────── */
    sig.bias = memory_query_bias(seq);
    if (sig.bias <= cfg->min_bias) {
        sig.block_gate = 2;
        return sig;
    }

    /* ── Eval + next-symbol prediction ───────────────────────── */
    Symbol best_sym;
    sig.delta      = predict_next(seq, &best_sym);
    sig.confidence = fmin(1.0, fabs(sig.delta) / 2000.0);
    sig.direction  = (sig.delta > 0) ? 1 : -1;

    /* ── λ3: Confidence / delta strength gate ──────────────────── */
    if (sig.confidence < cfg->min_confidence || fabs(sig.delta) < cfg->min_delta) {
        sig.block_gate = 3;
        return sig;
    }

    /* ── λ4: Geometric stability gate ─────────────────────────── */
    sig.energy = calc_energy(seq);
    if (sig.energy >= cfg->max_energy) {
        sig.block_gate = 4;
        return sig;
    }
    sig.curl = calc_curl(seq);
    if (sig.curl > cfg->curl_thresh) {
        sig.block_gate = 4;
        return sig;
    }

    /* ── λ5: Asset momentum bias gate ──────────────────────────── */
    /* cfg->asset_bias: 1=long-only, -1=short-only, 0=both */
    if (cfg->asset_bias != 0 && sig.direction != cfg->asset_bias) {
        sig.block_gate = 5;
        return sig;
    }

    /* ── λ6: Micro sanity (lot size computed later, checked here) */
    /* Will be validated again after kelly_size() in main loop    */

    /* ── λ7: Macro causal (optional — stub) ───────────────────── */
    /* TODO: check DXY / correlation asset if needed              */

    sig.valid = true;
    return sig;
}

/*
 * kelly_size()
 *
 * Half-Kelly with confidence and stability damping.
 * Returns fraction of capital to risk [MIN_POS_PCT, MAX_POS_PCT].
 *
 * base_kelly = (p*W - (1-p)*L) / W    where p=win_rate, W=avg_win, L=avg_loss
 */
double kelly_size(double win_rate, double avg_win, double avg_loss,
                  double conf, double stability, const Config *cfg) {
    if (avg_win < 1e-9) return cfg->risk_pct * MIN_POS_PCT;

    double loss_rate = 1.0 - win_rate;
    double base = (win_rate * avg_win - loss_rate * avg_loss) / avg_win;

    /* Half-Kelly × confidence × stability damping */
    double size = (base * 0.5) * conf * (1.0 - stability * 0.3);

    /* Clamp to configured risk bounds */
    double lo = cfg->risk_pct * MIN_POS_PCT;
    double hi = cfg->risk_pct * MAX_POS_PCT;
    if (size < lo) size = lo;
    if (size > hi) size = hi;

    return size;
}

/*
 * sl_pct_for_symbol()
 *
 * Returns the stop-loss distance as a fraction of entry price
 * for the predicted next symbol (conservative side).
 */
double sl_pct_for_symbol(Symbol s) {
    return SYM_SL_PCT[(int)s];
}
