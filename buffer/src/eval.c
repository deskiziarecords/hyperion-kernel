#include "../include/smart.h"
#include <math.h>

/*
 * evaluate_sequence()
 *
 * Returns a signed score:
 *   positive → bullish edge
 *   negative → bearish edge
 *
 * Score = Σ(material[i] × recency_weight[i]) + Σ(position_table[i][pos_idx])
 */
double evaluate_sequence(const Symbol seq[SEQ_LEN]) {
    double material = 0.0;
    double position = 0.0;

    for (int i = 0; i < SEQ_LEN; i++) {
        int s = (int)seq[i];

        /* Recency weight: older = less, newer = more */
        double w = (i + 1.0) / SEQ_LEN;
        material += SYM_VALUE[s] * w;

        /* Map sequence index 0..SEQ_LEN-1 → table index 0..63 */
        int tbl_idx = (int)(i * 63.0 / (SEQ_LEN - 1));
        if (tbl_idx > 63) tbl_idx = 63;
        position += POSITION_TABLES[s][tbl_idx];
    }

    return material + position;
}

/*
 * predict_next()
 *
 * Tries all 7 possible next symbols, picks the one that causes the
 * largest absolute delta from the current evaluation.
 * Returns the delta; writes the winning symbol into *best_sym.
 */
double predict_next(const Symbol seq[SEQ_LEN], Symbol *best_sym) {
    double base      = evaluate_sequence(seq);
    double best_abs  = -1.0;
    double best_delt = 0.0;
    Symbol candidate[SEQ_LEN];

    for (int s = 0; s < SYMBOLS; s++) {
        /* Shift left by one and append candidate symbol */
        for (int i = 0; i < SEQ_LEN - 1; i++) candidate[i] = seq[i + 1];
        candidate[SEQ_LEN - 1] = (Symbol)s;

        double delta = evaluate_sequence(candidate) - base;
        double absd  = fabs(delta);

        if (absd > best_abs) {
            best_abs  = absd;
            best_delt = delta;
            *best_sym = (Symbol)s;
        }
    }

    return best_delt;
}
