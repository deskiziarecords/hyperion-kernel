#include "../include/smart.h"
#include <math.h>
#include <string.h>

/* ─── Candle → Symbol ───────────────────────────────────────────── */
Symbol encode_candle(const Candle *c) {
    double body  = fabs(c->close - c->open);
    double range = c->high - c->low;

    if (range < 1e-9) return SYM_X;   /* degenerate candle */

    double ratio = body / range;
    double upper = c->high - fmax(c->open, c->close);
    double lower = fmin(c->open, c->close) - c->low;

    /* Wick overrides: long wick = rejection signal */
    if (upper > range * 0.6) return SYM_W;
    if (lower > range * 0.6) return SYM_w;

    /* Doji */
    if (ratio < 0.10) return SYM_X;

    /* Directional body */
    if (c->close > c->open)
        return (ratio > 0.6) ? SYM_B : SYM_U;
    else
        return (ratio > 0.6) ? SYM_I : SYM_D;
}

/* ─── Rolling Sequence Buffer ───────────────────────────────────── */
/* Ring buffer: always holds the last SEQ_LEN symbols in logical order */

void seqbuf_push(SeqBuffer *sb, Symbol s) {
    sb->seq[sb->ptr % SEQ_LEN] = s;
    sb->ptr++;
    if (sb->count < SEQ_LEN) sb->count++;
}

/*
 * Read logical order (oldest → newest) into out[SEQ_LEN].
 * If fewer than SEQ_LEN symbols have been seen, pad with SYM_X at the front.
 */
void seqbuf_read(const SeqBuffer *sb, Symbol out[SEQ_LEN]) {
    if (sb->count < SEQ_LEN) {
        int pad = SEQ_LEN - sb->count;
        for (int i = 0; i < pad; i++) out[i] = SYM_X;
        /* copy the count symbols we have in insertion order */
        int start = (sb->ptr - sb->count + SEQ_LEN) % SEQ_LEN;
        for (int i = 0; i < sb->count; i++)
            out[pad + i] = sb->seq[(start + i) % SEQ_LEN];
    } else {
        /* full buffer: start from oldest */
        int start = sb->ptr % SEQ_LEN;
        for (int i = 0; i < SEQ_LEN; i++)
            out[i] = sb->seq[(start + i) % SEQ_LEN];
    }
}

/* Render sequence as null-terminated string, buf must be SEQ_LEN+1 */
char *seq_to_str(const Symbol seq[SEQ_LEN], char buf[SEQ_LEN+1]) {
    for (int i = 0; i < SEQ_LEN; i++) buf[i] = SYM_CHAR[(int)seq[i]];
    buf[SEQ_LEN] = '\0';
    return buf;
}
