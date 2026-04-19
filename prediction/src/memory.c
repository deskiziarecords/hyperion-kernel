#include "../include/smart.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdio.h>

/* ─── Storage ───────────────────────────────────────────────────── */
static MemoryEntry db[MEMORY_CAP];
static int db_count = 0;
static int db_ring  = 0;   /* next write slot (ring) */

/* ─── Helpers ───────────────────────────────────────────────────── */
static void seq_to_onehot(const Symbol seq[SEQ_LEN], float vec[DIM]) {
    memset(vec, 0, DIM * sizeof(float));
    for (int i = 0; i < SEQ_LEN; i++)
        vec[i * SYMBOLS + (int)seq[i]] = 1.0f;
}

static float l2_sq(const float a[DIM], const float b[DIM]) {
    float d = 0.0f;
    for (int i = 0; i < DIM; i++) {
        float diff = a[i] - b[i];
        d += diff * diff;
    }
    return d;
}

/* ─── Public API ────────────────────────────────────────────────── */

void memory_init(void) {
    db_count = 0;
    db_ring  = 0;
}

void memory_store(const Symbol seq[SEQ_LEN], double pnl) {
    int slot = db_ring % MEMORY_CAP;
    memcpy(db[slot].seq, seq, SEQ_LEN * sizeof(Symbol));
    db[slot].pnl = pnl;
    db_ring++;
    if (db_count < MEMORY_CAP) db_count++;
}

/*
 * memory_query_bias()
 *
 * Exact L2 KNN (k=KNN_K) over 140-dim one-hot vectors.
 * Returns average PnL of the k nearest neighbours.
 * Returns 0.0 if insufficient memory.
 */
double memory_query_bias(const Symbol seq[SEQ_LEN]) {
    if (db_count == 0) return 0.0;

    float query[DIM];
    seq_to_onehot(seq, query);

    /* Keep top-K smallest distances with indices */
    float top_dist[KNN_K];
    int   top_idx [KNN_K];
    int   found = 0;

    for (int i = 0; i < KNN_K; i++) {
        top_dist[i] = FLT_MAX;
        top_idx[i]  = -1;
    }

    for (int i = 0; i < db_count; i++) {
        float vec[DIM];
        seq_to_onehot(db[i].seq, vec);
        float dist = l2_sq(query, vec);

        /* Insert into sorted top-K (insertion sort, K=5 → trivial) */
        if (dist < top_dist[KNN_K - 1]) {
            top_dist[KNN_K - 1] = dist;
            top_idx [KNN_K - 1] = i;
            /* Bubble up */
            for (int j = KNN_K - 1; j > 0; j--) {
                if (top_dist[j] < top_dist[j-1]) {
                    float fd = top_dist[j]; top_dist[j] = top_dist[j-1]; top_dist[j-1] = fd;
                    int   fi = top_idx [j]; top_idx [j] = top_idx [j-1]; top_idx [j-1] = fi;
                } else break;
            }
            if (found < KNN_K) found++;
        }
    }

    if (found == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < found; i++)
        sum += db[top_idx[i]].pnl;
    return sum / found;
}

int memory_count(void) { return db_count; }

/* ─── Persistence ───────────────────────────────────────────────── */
/*
 * Binary format:
 *   4 bytes: magic 0x534D454D ('SMEM')
 *   4 bytes: db_count (int32)
 *   4 bytes: db_ring  (int32)
 *   N × sizeof(MemoryEntry): entries
 */
#define MEM_MAGIC 0x534D454Du

void memory_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror("memory_save"); return; }

    uint32_t magic   = MEM_MAGIC;
    int32_t  cnt     = (int32_t)db_count;
    int32_t  ring    = (int32_t)db_ring;

    fwrite(&magic, sizeof magic, 1, f);
    fwrite(&cnt,   sizeof cnt,   1, f);
    fwrite(&ring,  sizeof ring,  1, f);

    /* Write in logical order (oldest first) */
    int start = (db_count < MEMORY_CAP) ? 0 : (db_ring % MEMORY_CAP);
    for (int i = 0; i < db_count; i++)
        fwrite(&db[(start + i) % MEMORY_CAP], sizeof(MemoryEntry), 1, f);

    fclose(f);
}

void memory_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { return; }   /* not an error — file may not exist yet */

    uint32_t magic;
    int32_t  cnt, ring;

    if (fread(&magic, sizeof magic, 1, f) != 1 || magic != MEM_MAGIC) {
        fclose(f); return;
    }
    if (fread(&cnt,  sizeof cnt,  1, f) != 1) { fclose(f); return; }
    if (fread(&ring, sizeof ring, 1, f) != 1) { fclose(f); return; }

    if (cnt < 0 || cnt > MEMORY_CAP) { fclose(f); return; }

    for (int i = 0; i < cnt; i++)
        if (fread(&db[i], sizeof(MemoryEntry), 1, f) != 1) break;

    db_count = cnt;
    db_ring  = ring;
    fclose(f);
}
