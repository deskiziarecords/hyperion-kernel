#ifndef SMART_H
#define SMART_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ─── Core Parameters ──────────────────────────────────────────── */
#define SEQ_LEN          20
#define SYMBOLS          7
#define DIM              (SEQ_LEN * SYMBOLS)   /* 140 */
#define MEMORY_CAP       10000
#define KNN_K            5

/* ─── Default Thresholds (overridable via config.json) ─────────── */
#define ENTROPY_THRESH   0.60
#define ENERGY_THRESH    0.50
#define CURL_THRESH      0.80
#define MIN_CONFIDENCE   0.60
#define MIN_BIAS         0.10
#define MIN_DELTA        500.0
#define MAX_POS_PCT      0.02
#define MIN_POS_PCT      0.005

/* ─── Symbol Definitions ────────────────────────────────────────── */
typedef enum {
    SYM_B = 0,   /* Bull Queen  - strong bullish body  >0.6× range */
    SYM_I = 1,   /* Bear Queen  - strong bearish body  >0.6× range */
    SYM_W = 2,   /* Upper Wick  - rejection from highs             */
    SYM_w = 3,   /* Lower Wick  - rejection from lows              */
    SYM_U = 4,   /* Weak Bull   - small bullish body <0.3× range   */
    SYM_D = 5,   /* Weak Bear   - small bearish body <0.3× range   */
    SYM_X = 6    /* Neutral     - doji or inside bar               */
} Symbol;

/* Character representation */
static const char SYM_CHAR[7] = {'B','I','W','w','U','D','X'};

/* Chess-piece material values */
static const int SYM_VALUE[7] = {900, -900, 500, -500, 330, -320, 100};

/* Stop-loss percentages per symbol (as fractions, e.g. 0.008 = 0.8%) */
static const double SYM_SL_PCT[7] = {0.008, 0.008, 0.006, 0.006, 0.010, 0.010, 0.005};

/* ─── Position Tables (7 symbols × 64 cells = 448 bytes) ────────── */
/* Maps sequence position → bonus/penalty for that symbol */
static const int POSITION_TABLES[7][64] = {
    /* B */ {-20,-15,-10,-5,-5,-10,-15,-20,-10,0,0,5,5,0,0,-10,
             -10,5,10,15,15,10,5,-10,-5,0,15,20,20,15,0,-5,
             -5,5,15,25,25,15,5,-5,-10,0,10,20,20,10,0,-10,
              10,20,30,40,40,30,20,10,50,50,55,60,60,55,50,50},
    /* I */ {-5,-5,-5,-6,-6,-5,-5,-5,-1,-2,-3,-4,-4,-3,-2,-1,
              1,0,-1,-1,-1,-1,0,1,0,0,-1,-2,-2,-1,0,0,
              0,0,-1,-2,-2,-1,0,0,1,0,-1,-1,-1,-1,0,1,
              2,1,1,0,0,1,1,2,2,1,1,0,0,1,1,2},
    /* W */ {0,0,0,0,0,0,0,0,-1,0,0,1,1,0,0,-1,-1,0,1,2,2,1,0,-1,
              0,0,1,2,2,1,0,0,0,0,1,2,2,1,0,0,-1,0,1,1,1,1,0,-1,
              0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* w */ {0,0,0,0,0,0,0,0,1,0,-1,-1,-1,-1,0,1,0,0,-1,-2,-2,-1,0,0,
              0,0,-1,-2,-2,-1,0,0,0,0,-1,-2,-2,-1,0,0,1,0,-1,-1,-1,-1,0,1,
              0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* U */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
              0,0,1,2,2,1,0,0,0,0,1,2,2,1,0,0,1,1,2,3,3,2,1,1,
              4,4,4,5,5,4,4,4,0,0,0,0,0,0,0,0},
    /* D */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-1,-1,0,0,0,
              0,0,-1,-2,-2,-1,0,0,0,0,-1,-2,-2,-1,0,0,-1,-1,-2,-3,-3,-2,-1,-1,
             -4,-4,-4,-5,-5,-4,-4,-4,0,0,0,0,0,0,0,0},
    /* X */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
              0,0,0,1,1,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,
              0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

/* ─── Data Structures ───────────────────────────────────────────── */
typedef struct {
    double   open, high, low, close;
    uint64_t timestamp_ms;
    uint64_t volume;
    double   spread;
} Candle;

typedef struct {
    Symbol  seq[SEQ_LEN];
    double  pnl;       /* pips */
} MemoryEntry;

typedef struct {
    char    asset[16];
    char    api_key[128];
    char    account_id[64];
    bool    practice;
    bool    paper;
    double  capital;
    double  risk_pct;
    double  entropy_thresh;
    double  min_confidence;
    double  min_bias;
    double  max_energy;
    double  curl_thresh;
    double  min_delta;
    double  max_daily_loss_pct;
    int     log_level;      /* 0=QUIET 1=INFO 2=DEBUG */
    char    log_path[256];
    /* Asset momentum bias: 1=long-only, -1=short-only, 0=both */
    int     asset_bias;
} Config;

typedef struct {
    double  confidence;
    double  bias;
    double  energy;
    double  curl;
    double  entropy;
    double  delta;
    double  lot_size;
    bool    valid;
    int     direction;    /* 1=LONG, -1=SHORT */
    int     block_gate;   /* which λ-gate blocked: 0=none, 1-7=gate# */
} Signal;

typedef struct {
    Symbol  seq[SEQ_LEN];
    int     ptr;          /* rolling write pointer */
    int     count;        /* how many candles ingested so far */
} SeqBuffer;

typedef struct {
    uint64_t timestamp_ms;
    int       direction;
    double    entry_price;
    double    sl_price;
    double    tp_price;
    double    lot_size;
    Signal    signal;
    Symbol    entry_seq[SEQ_LEN];
} OpenPosition;

/* ─── Forward Declarations ─────────────────────────────────────── */

/* pattern.c */
Symbol  encode_candle(const Candle *c);
void    seqbuf_push(SeqBuffer *sb, Symbol s);
void    seqbuf_read(const SeqBuffer *sb, Symbol out[SEQ_LEN]);
char   *seq_to_str(const Symbol seq[SEQ_LEN], char buf[SEQ_LEN+1]);

/* eval.c */
double  evaluate_sequence(const Symbol seq[SEQ_LEN]);
double  predict_next(const Symbol seq[SEQ_LEN], Symbol *best_sym);

/* entropy.c */
double  calc_entropy(const Symbol seq[SEQ_LEN]);

/* memory.c */
void    memory_init(void);
void    memory_store(const Symbol seq[SEQ_LEN], double pnl);
double  memory_query_bias(const Symbol seq[SEQ_LEN]);
int     memory_count(void);
void    memory_save(const char *path);
void    memory_load(const char *path);

/* geometry.c */
double  calc_energy(const Symbol seq[SEQ_LEN]);
double  calc_divergence(const Symbol seq[SEQ_LEN]);
double  calc_curl(const Symbol seq[SEQ_LEN]);

/* risk.c */
Signal  evaluate_signal(const Symbol seq[SEQ_LEN], const Config *cfg);
double  kelly_size(double win_rate, double avg_win, double avg_loss,
                   double conf, double stability, const Config *cfg);

/* oanda.c */
bool    oanda_connect(const Config *cfg);
bool    oanda_fetch_candle(const Config *cfg, Candle *out);
bool    oanda_place_order(const Config *cfg, const Signal *sig,
                          double entry_price, OpenPosition *pos_out);
void    oanda_close_position(const Config *cfg, const OpenPosition *pos,
                             double current_price, double *pnl_out);
bool    oanda_get_price(const Config *cfg, double *bid, double *ask);
void    oanda_disconnect(void);

/* config.c */
bool    config_load(const char *path, Config *cfg);
void    config_defaults(Config *cfg);

/* log.c */
void    log_init(const Config *cfg);
void    log_info(const char *fmt, ...);
void    log_debug(const char *fmt, ...);
void    log_trade(const OpenPosition *pos, double pnl, const char *outcome);
void    log_blocked(const Signal *sig, const char *seq_str);
void    log_close(void);

/* backtest.c */
int     backtest_run(const char *csv_path, const Config *cfg);

#endif /* SMART_H */
