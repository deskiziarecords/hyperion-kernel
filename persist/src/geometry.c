#include "../include/smart.h"
#include <math.h>

/*
 * We use a fixed 7×4 embedding (28 floats, 112 bytes).
 * Each symbol maps to a 4-D point in pattern space.
 * Chosen so that directionally similar symbols cluster together.
 *
 * Layout: B, I, W, w, U, D, X
 */
static const float EMBEDDING[7][4] = {
    /*B*/  { 1.0f,  0.8f,  0.3f,  0.0f},
    /*I*/  {-1.0f, -0.8f, -0.3f,  0.0f},
    /*W*/  { 0.6f,  0.2f, -0.8f,  0.5f},
    /*w*/  {-0.6f, -0.2f,  0.8f, -0.5f},
    /*U*/  { 0.4f,  0.3f,  0.1f,  0.2f},
    /*D*/  {-0.4f, -0.3f, -0.1f, -0.2f},
    /*X*/  { 0.0f,  0.0f,  0.0f,  0.0f}
};
#define EDIM 4

/*
 * calc_energy()
 *
 * Sum of squared differences between consecutive embedding vectors,
 * plus a curvature term (second differences).
 * Lower energy = more stable / coherent pattern.
 * Normalized to approximately [0, 1].
 */
double calc_energy(const Symbol seq[SEQ_LEN]) {
    float diff[SEQ_LEN - 1][EDIM];
    double energy = 0.0;

    /* First differences */
    for (int i = 0; i < SEQ_LEN - 1; i++) {
        for (int d = 0; d < EDIM; d++) {
            diff[i][d] = EMBEDDING[(int)seq[i+1]][d] - EMBEDDING[(int)seq[i]][d];
            energy += diff[i][d] * diff[i][d];
        }
    }

    /* Second differences (curvature) */
    for (int i = 0; i < SEQ_LEN - 2; i++) {
        for (int d = 0; d < EDIM; d++) {
            float curv = diff[i+1][d] - diff[i][d];
            energy += curv * curv;
        }
    }

    /* Normalize: max theoretical energy ≈ (SEQ_LEN-1)×4×4 + (SEQ_LEN-2)×4×4 */
    double max_e = ((SEQ_LEN - 1) + (SEQ_LEN - 2)) * EDIM * 4.0;
    return energy / max_e;
}

/*
 * calc_divergence()
 *
 * Trend acceleration: compare recent 10 vs older 10 symbols.
 * Positive = trend gaining momentum, negative = contracting.
 */
double calc_divergence(const Symbol seq[SEQ_LEN]) {
    double older = 0.0, recent = 0.0;
    for (int i = 0; i < 10; i++)  older  += SYM_VALUE[(int)seq[i]];
    for (int i = 10; i < 20; i++) recent += SYM_VALUE[(int)seq[i]];
    /* Normalize by max possible value difference */
    return (recent - older) / (10.0 * 900.0);
}

/*
 * calc_curl()
 *
 * Rotational chaos: fraction of consecutive symbol pairs where
 * the directional bias flips (bull→bear or bear→bull).
 * High curl = frequent flip = uncertain / choppy.
 */
double calc_curl(const Symbol seq[SEQ_LEN]) {
    static const int BULLISH[7] = {1, 0, 1, 0, 1, 0, 0};  /* B W U */
    int flips = 0;
    for (int i = 1; i < SEQ_LEN; i++) {
        int a = BULLISH[(int)seq[i-1]];
        int b = BULLISH[(int)seq[i]];
        /* Count flip only when neither is neutral (X or D/I w) */
        int na = ((int)seq[i-1] != SYM_X);
        int nb = ((int)seq[i]   != SYM_X);
        if (na && nb && (a != b)) flips++;
    }
    return flips / (double)(SEQ_LEN - 1);
}
