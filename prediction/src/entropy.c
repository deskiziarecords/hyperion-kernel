#include "../include/smart.h"
#include <math.h>

/*
 * calc_entropy()
 *
 * Shannon entropy of the symbol frequency distribution,
 * normalized to [0, 1] where:
 *   0.0 = perfectly predictable (one repeated symbol)
 *   1.0 = maximally random (all 7 symbols equally frequent)
 */
double calc_entropy(const Symbol seq[SEQ_LEN]) {
    int counts[SYMBOLS] = {0};
    for (int i = 0; i < SEQ_LEN; i++) counts[(int)seq[i]]++;

    static const double MAX_H = 2.80735;  /* log2(7) */
    double H = 0.0;

    for (int i = 0; i < SYMBOLS; i++) {
        if (counts[i] == 0) continue;
        double p = counts[i] / (double)SEQ_LEN;
        H -= p * log2(p);
    }

    return H / MAX_H;
}
