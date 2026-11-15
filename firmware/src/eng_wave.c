/* SPDX-License-Identifier: GPL-3.0-only */
/* WAVE: a wavetable synth in the manner of Xfer's Serum, cut to what this unit carries. The sound the designer
 * (EDIT in the new layout: ui_design.c) makes.
 *
 * Two oscillators, A and B, each with a TABLE of its own: 6 frames, each one cycle of a wave; POS moves through them
 * (the two frames beside it are mixed), and the SHP routings move both POS (ENV DEST / LFO DEST SHAPE, the matrix's
 * SHP): the sweep through a table is the sound. Each has its LEVEL, its pitch beside the note's (OCT, SEMI, FINE in
 * cents), a WARP (how its cycle is read: SYNC faster and windowed, BEND squeezed to its start or its end, PWM into
 * a part of it with silence after, FM moved by the other oscillator) with its amount, and THICK: two more copies of
 * it, a little above and below (three for one, at three times the work). Under them a SUB (a sine, a deeper one, a
 * triangle, a square: an octave or two below the note) and NOISE (its LEVEL, its COLOUR from a rumble to a hiss).
 * Then DRIVE and the filter: LOW 12, LOW 24, BAND, HIGH, NOTCH, its CUTOFF following the keys by KEY TRK; each of
 * the four sources goes through it or around it.
 *
 * The values. The eight of every engine (0 .. 127): E0 the two tables (A + 8 B), E1 A's POS, E2 B's POS, E3 SUB,
 * E4 CUTOFF, E5 RES, E6 A's WARP amount, E7 DRIVE. And sixteen more (WX_*), in the values DIGITAL's operators had
 * (P_FM1_ATK on: a build has DIGITAL or WAVE, never both, engines.c eng_ok; every store, undo and the automation
 * carry them as the engine's own, motion.c motion_engine_id). A factory sound sets them from WV_PX (ui.c preset_fill).
 *
 * The tables are not stored: a table is two spectra (WV_TABLES: which harmonics, how loud, at each end of it) and
 * its frames are built from them when the sound asks for it, by adding sines, in the part's shared area (engines.c
 * eng_mem), a little each audio block (WV_WORK multiply-adds: a table is whole after about a fifth of a second, A's
 * first, and plays from the fourth block on). Each frame is built five times, with 64, 32, 16, 8 and 4 harmonics
 * (256 .. 16 samples): a note plays the one whose harmonics stay under half the sample rate, so high notes do not
 * alias (a WARP reads the cycle faster: the level for that speed). Every frame has the same power, so POS changes
 * the colour, not the level.
 *
 * State: the part's tables (wv_part_t, 12.9 KB: heavy tracks only, as GRAIN and PHYS); per voice ph[0] A, ph[1] B,
 * ph[2] the sub, s[0..3] the filter's two stages, s[4] the noise's generator, s[5] its colour's filter, s[6], s[7]
 * how far A's and B's THICK copies have moved from them. */
#define ENGI_WAVE 15u            /* engines.c ENGINES[] (append-only) */
#define WV_NT 2u                 /* the tables of a part: A's, B's */
#define WV_NF 6u                 /* frames of a table */
#define WV_NL 5u                 /* levels of a frame: 256, 128, 64, 32, 16 samples; 64 .. 4 harmonics */
#define WV_FS 496u               /* a frame's samples, all levels */
#define WV_H0 64u
#define WV_RMS 9000              /* every frame's level (its peaks: a saw's 15,600, a narrow pulse's clipped) */
#define WV_WORK 512u             /* multiply-adds a control tick (32 samples) while a table is built */
static const uint16_t WV_OFF[WV_NL] = {0, 256, 384, 448, 480};
static const uint8_t WV_ORDER[WV_NL] = {2, 3, 4, 1, 0};   /* the order they are built in: playable soon, whole later */

#define WX(k) (P_FM1_ATK + (k))
enum { WX_ALVL, WX_BLVL,         /* LEVEL 0 .. 127 (a square law: 127 as it is, 90 half) */
       WX_AWARP, WX_BWARP,       /* WARP's kind (WK_*) + 8 (OCT + 3) */
       WX_ASEMI, WX_BSEMI,       /* SEMI + 12: 0 .. 24 */
       WX_AFINE, WX_BFINE,       /* FINE + 64: cents, 1 .. 127 */
       WX_ATHICK, WX_BTHICK,     /* THICK 0 (one copy) .. 127 (three, about 50 cents apart) */
       WX_BAMT,                  /* B's WARP amount (A's: E6) */
       WX_SUBK,                  /* SUB's shape (WS_*) */
       WX_NLVL, WX_NCOL,         /* NOISE: LEVEL, COLOUR (0 a rumble .. 127 white) */
       WX_FLT,                   /* the filter's kind (WF_*) + 8 x what goes through it (bit 0 A, 1 B, 2 NOISE, 3 SUB) */
       WX_KTRK,                  /* KEY TRK: 0 none, 64 as ANALOG's, 127 twice that */
       WX_N };
_Static_assert(WX(WX_N - 1u) <= P_FM4_LEVEL, "WAVE: its values in DIGITAL's operators'");
enum { WK_OFF, WK_SYNC, WK_BENDP, WK_BENDM, WK_PWM, WK_FM, WK_N };
enum { WS_SINE, WS_DEEP, WS_TRI, WS_SQUARE, WS_N };
enum { WF_LOW, WF_LOW24, WF_BAND, WF_HIGH, WF_NOTCH, WF_N };
enum { WR_A = 1, WR_B = 2, WR_NOISE = 4, WR_SUB = 8, WR_ALL = 15 };
static const char *const N_WV_KIND[WK_N] = {"OFF", "SYNC", "BEND+", "BEND-", "PWM", "FM"};
static const char *const N_WV_SUB[WS_N] = {"SINE", "DEEP", "TRI", "SQUARE"};
static const char *const N_WV_FLT[WF_N] = {"LOW 12", "LOW 24", "BAND", "HIGH", "NOTCH"};
#define WVT(a, b) ((a) | (b) << 3)                  /* E0: A's table, B's */
#define WVA(kind, oct) ((kind) | ((oct) + 3) << 3)  /* WX_AWARP, WX_BWARP: the WARP's kind, OCT -3 .. +3 */
#define WVF(kind, through) ((kind) | (through) << 3)   /* WX_FLT */
static int32_t wv_x(const int16_t *p, uint32_t k) { return clamp(p[WX(k)], 0, 127); }
static uint32_t wv_tab(const int16_t *p, uint32_t o)    /* oscillator o's table */
{
    uint32_t v = (uint32_t)clamp(p[P_E0], 0, 63);
    return o ? v >> 3 : v & 7u;
}
static uint32_t wv_kind(const int16_t *p, uint32_t o)
{
    uint32_t k = (uint32_t)wv_x(p, WX_AWARP + o) & 7u;
    return k < WK_N ? k : WK_OFF;
}
static int32_t wv_oct(const int16_t *p, uint32_t o) { return clamp(wv_x(p, WX_AWARP + o) >> 3, 0, 6) - 3; }
static int32_t wv_semi(const int16_t *p, uint32_t o) { return clamp(wv_x(p, WX_ASEMI + o), 0, 24) - 12; }
static int32_t wv_fine(const int16_t *p, uint32_t o) { return wv_x(p, WX_AFINE + o) - 64; }
static int32_t wv_amt(const int16_t *p, uint32_t o) { return o ? wv_x(p, WX_BAMT) : clamp(p[P_E6], 0, 127); }
static uint32_t wv_fkind(const int16_t *p)
{
    uint32_t k = (uint32_t)wv_x(p, WX_FLT) & 7u;
    return k < WF_N ? k : WF_LOW;
}
static uint32_t wv_through(const int16_t *p) { return ((uint32_t)wv_x(p, WX_FLT) >> 3) & 15u; }
static uint32_t wv_subk(const int16_t *p) { return (uint32_t)wv_x(p, WX_SUBK) % WS_N; }
/* an oscillator's pitch beside the note's, in cents */
static int32_t wv_cents(const int16_t *p, uint32_t o) { return wv_oct(p, o) * 1200 + wv_semi(p, o) * 100 + wv_fine(p, o); }

typedef struct {                 /* a spectrum: an end of a table (the frames between: every value on the way) */
    uint8_t top;                 /* its harmonics: 1 .. 64 */
    uint8_t tilt;                /* how fast they fall: 0 as 1/h (a saw's) .. 255 as 1/h^2 (soft) */
    uint8_t rest;                /* the harmonics off the table's comb (PER 2: the even ones): 255 as the others, 0 none */
    uint8_t duty;                /* a pulse's width, /512 of the cycle (255: a square); 0: no pulse */
    uint8_t fc, fw, fg;          /* a formant: its harmonic, its width in harmonics, its gain (x 1 + fg / 16); fc 0 none */
    uint8_t rnd;                 /* each harmonic's level scattered (a hash): 0 not .. 255 all the way down */
} wv_spec_t;
enum { WF_ALT = 1, WF_PHASE = 2, WF_STEP = 4, WF_OCT = 8 };   /* odd harmonics' signs alternate (a triangle's);
                                  * every harmonic at a phase of its own; every frame its own scatter; only the
                                  * octaves (1 2 4 8 ..) */
typedef struct {
    const char *name;
    wv_spec_t a, b;              /* the first frame's, the last one's */
    uint8_t per, seed, flags;    /* the comb: harmonics 1, 1 + per, 1 + 2 per .. are the table's own (2: the odd ones) */
} wv_table_t;
static const wv_table_t WV_TABLES[] = {
    /*           top tilt rest duty  fc  fw   fg  rnd     top tilt rest duty  fc  fw   fg  rnd   per seed flags */
    {"BASIC",   {  1, 255, 255,   0,  0,  0,   0,   0}, { 64,   0, 255,   0,  0,  0,   0,   0}, 2, 0, 0},   /* sine .. saw */
    {"HOLLOW",  {  1, 255,   0,   0,  0,  0,   0,   0}, { 64,   0,   0,   0,  0,  0,   0,   0}, 2, 0, 0},   /* sine .. square */
    {"PULSE",   { 64,   0, 255, 255,  0,  0,   0,   0}, { 64,   0, 255,  45,  0,  0,   0,   0}, 2, 0, 0},   /* square .. 9 % (narrower: its peaks
                                                                                                             * past 16 bits at this level) */
    {"FORMANT", { 64,  90, 255,   0,  2,  2, 110,   0}, { 64,  90, 255,   0, 24,  5, 110,   0}, 2, 0, 0},   /* a vowel going up */
    {"GROWL",   { 48,  40, 110,   0,  3,  2, 150,  90}, { 64,   0, 200,   0, 13,  4, 150, 170}, 2, 7, WF_PHASE},
    {"DIGITAL", { 24,  20, 255,   0,  0,  0,   0, 235}, { 64,   0, 255,   0,  0,  0,   0, 235}, 2, 3, WF_STEP},
    {"BELL",    { 10, 120, 110,   0,  0,  0,   0,   0}, { 40,  10,  12,   0,  7,  3,  40,   0}, 3, 0, 0},   /* 1 4 7 10 .. */
    {"ORGAN",   {  1, 255, 255,   0,  0,  0,   0,   0}, { 64,  30, 255,   0,  0,  0,   0,   0}, 2, 0, WF_OCT},
};
#define WV_NTAB ((uint32_t)NELEM(WV_TABLES))
static const char *const N_WV_TABLE[] = {"BASIC", "HOLLOW", "PULSE", "FORMANT", "GROWL", "DIGITAL", "BELL", "ORGAN"};
_Static_assert(NELEM(N_WV_TABLE) == NELEM(WV_TABLES), "WAVE: a name per table");

typedef struct {
    int16_t tab[WV_NT][WV_NF][WV_FS];   /* A's frames, B's: their levels after one another (WV_OFF) */
    int32_t acc[256];            /* the frame being built */
    uint16_t pos[WV_NT];         /* POS as the last voice played it, 0 .. 127 << 8 (the pages' pictures) */
    uint16_t gain;               /* the frame being built: its level, Q10 */
    uint8_t live;                /* 0: the area as an engine switch leaves it, nothing built */
    uint8_t table[WV_NT];        /* the table built there, or being built; 0xFF none */
    uint8_t ready[WV_NT];        /* bit l: level l is whole */
    uint8_t bt;                  /* building: A's (0) or B's (1) */
    uint8_t stage, frame, h;     /* .. WV_ORDER[stage]'s level, this frame, its harmonics done; stage WV_NL: none */
    uint8_t open;                /* .. that frame is begun (its level known, acc cleared) */
} wv_part_t;

static wv_part_t *wv_of(const track_t *t)             /* (a light track has no room for it: no WAVE there, eng_allowed) */
{
    if (t < &trk[0] || t >= &trk[NHEAVY])
        return 0;
    return (wv_part_t *)eng_part(t);
}
static uint32_t wv_hash(uint32_t x)
{
    x *= 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    return x ^ x >> 13;
}
/* harmonic h (1 .. 64) of frame f of table T: its level, signed, +-32767 */
static int32_t wv_amp(const wv_table_t *T, uint32_t f, uint32_t h)
{
#define WV_MID(m) ((int32_t)T->a.m + ((int32_t)T->b.m - (int32_t)T->a.m) * (int32_t)f / (int32_t)(WV_NF - 1u))
    int32_t top = WV_MID(top), tilt = WV_MID(tilt), rest = WV_MID(rest), duty = WV_MID(duty), fc = WV_MID(fc),
            fw = WV_MID(fw), fg = WV_MID(fg), rnd = WV_MID(rnd), a, a2, d;
#undef WV_MID
    if ((int32_t)h > top || ((T->flags & WF_OCT) && (h & (h - 1u))))
        return 0;
    a = 32767 / (int32_t)h;
    a2 = 32767 / (int32_t)(h * h);
    a += ((a2 - a) * tilt) >> 8;
    if ((h - 1u) % T->per)
        a = (a * rest) >> 8;
    if (fc && fw) {
        d = (int32_t)h - fc;
        d = d < 0 ? -d : d;
        if (d < fw)
            a += a * fg * (fw - d) / (fw * 16);
    }
    if (rnd)
        a = (a * (256 - ((rnd * (int32_t)(wv_hash(T->seed * 977u + ((T->flags & WF_STEP) ? f * 131u : 0u) + h) & 255u)) >> 8))) >> 8;
    if (a > 32767)
        a = 32767;
    if (duty)                                         /* a pulse d of the cycle wide: sin(pi h d), its sign too */
        a = (a * sine_i(h * (uint32_t)duty << 22)) >> 15;
    if ((T->flags & WF_ALT) && (h & 2u))
        a = -a;
    return a;
}
/* where harmonic h starts in the cycle, /1024 */
static uint32_t wv_phase(const wv_table_t *T, uint32_t f, uint32_t h)
{
    (void)f;
    if (T->a.duty || T->b.duty)                       /* a pulse: its harmonics are cosines (as sines they peak without
                                                       * end at its edges) */
        return 256u;
    return (T->flags & WF_PHASE) ? wv_hash(T->seed * 31u + h * 7u + 5u) & 1023u : 0u;
}

static void wv_begin(wv_part_t *P, uint32_t o, uint32_t table)
{
    if (P->table[o] == 0xFFu)
        P->ready[o] = 0;                              /* (else what is there plays on while the new one is built over it) */
    P->bt = (uint8_t)o;
    P->table[o] = (uint8_t)table;
    P->stage = P->frame = P->h = P->open = 0;
}
/* a block's share of the building */
static __attribute__((noinline)) void wv_work(wv_part_t *P)
{
    const wv_table_t *T = &WV_TABLES[P->table[P->bt] % WV_NTAB];
    uint32_t budget = WV_WORK;
    while (P->stage < WV_NL) {
        uint32_t L = WV_ORDER[P->stage], N = 256u >> L, H = WV_H0 >> L, step = 4u << L, f = P->frame, n, h;
        if (!P->open) {                               /* a frame begins: its level from all its harmonics, whatever the
                                                       * level keeps (a high note loses the top ones, the rest stay) */
            uint32_t sq = 0, r;
            if (budget < 512u)
                return;
            budget -= 512u;
            for (h = 1; h <= WV_H0; h++) {
                int32_t a = wv_amp(T, f, h) >> 6;
                sq += (uint32_t)(a * a);
            }
            r = isqrt32(sq >> 1) << 6;                /* the wave's RMS, as the levels count */
            P->gain = (uint16_t)clamp((int32_t)(((uint32_t)WV_RMS << 10) / (r ? r : 1u)), 1, 65535);
            for (n = 0; n < N; n++)
                P->acc[n] = 0;
            P->open = 1;
        }
        while (P->h < H) {
            int32_t a;
            uint32_t ph, d;
            if (budget < N)
                return;
            h = ++P->h;
            a = wv_amp(T, f, h);
            if (!a) {
                budget -= budget < 16u ? budget : 16u;
                continue;
            }
            budget -= N;
            ph = wv_phase(T, f, h);
            d = (h * step) & 1023u;
            for (n = 0; n < N; n++) {
                P->acc[n] += (a * SINE[ph]) >> 8;
                ph = (ph + d) & 1023u;
            }
        }
        for (n = 0; n < N; n++)                       /* whole: into the table at its level */
            P->tab[P->bt][f][WV_OFF[L] + n] = (int16_t)clamp(((P->acc[n] >> 7) * (int32_t)P->gain) >> 10, -32767, 32767);
        P->h = P->open = 0;
        if (++P->frame >= WV_NF) {
            P->frame = 0;
            P->ready[P->bt] |= (uint8_t)(1u << L);
            P->stage++;
        }
    }
}
static void wave_block(track_t *t)
{
    wv_part_t *P = wv_of(t);
    uint32_t o;
    if (!P)
        return;
    if (!P->live) {                                   /* the area as the engine switch left it: no table yet */
        P->live = 1;
        P->table[0] = P->table[1] = 0xFFu;
        P->ready[0] = P->ready[1] = 0;
        P->stage = WV_NL;
    }
    if (P->stage < WV_NL && P->table[P->bt] != wv_tab(t->p, P->bt))
        wv_begin(P, P->bt, wv_tab(t->p, P->bt));      /* (the one being built is not asked for any more: the other) */
    for (o = 0; o < WV_NT && P->stage >= WV_NL; o++)  /* nothing being built: one that is asked for, A's first */
        if (P->table[o] != wv_tab(t->p, o))
            wv_begin(P, o, wv_tab(t->p, o));
    if (P->stage < WV_NL)
        wv_work(P);
}

/* the notes at once the sound leaves room for (voice.c trk_nvoice): a THICK oscillator is read three times a sample, so
 * 8 notes as it is, 6 with one oscillator THICK, 4 with both: 24 readings a sample at most, where two plain ones take 16 */
static uint32_t wave_poly(const int16_t *p)
{
    uint32_t u = (wv_x(p, WX_ALVL) ? (wv_x(p, WX_ATHICK) ? 3u : 1u) : 0u) +
                 (wv_x(p, WX_BLVL) ? (wv_x(p, WX_BTHICK) ? 3u : 1u) : 0u);
    return u > 4u ? 4u : u > 2u ? 6u : 8u;
}

static void wave_note_on(track_t *t, voice_t *v)
{
    static uint32_t seed;                             /* (the THICK copies of every note start somewhere else) */
    (void)t;
    seed = seed * 1664525u + 1013904223u;
    v->ph[1] = v->ph[0] + 0x55555555u;
    v->ph[2] = v->ph[0] >> 1;
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;        /* filter */
    v->s[4] = (int32_t)(seed | 1u);
    v->s[5] = 0;
    v->s[6] = (int32_t)(seed >> 3);
    v->s[7] = (int32_t)((seed >> 5) * 3u);
}

/* one oscillator's sample: the cycle at ph in frames a and b (their level's tables, 256 >> (sh - 24) samples), pf / 256
 * of the way from a to b */
static inline int32_t wv_read(const int16_t *a, const int16_t *b, uint32_t ph, uint32_t sh, uint32_t mask, int32_t pf)
{
    uint32_t i = ph >> sh, j = (i + 1u) & mask;
    int32_t fr = (int32_t)((ph >> (sh - 14u)) & 0x3FFFu);
    int32_t x = a[i] + (((a[j] - a[i]) * fr) >> 14), y = b[i] + (((b[j] - b[i]) * fr) >> 14);
    return x + (((y - x) * pf) >> 8);
}
/* the level of table o whose harmonics stay under 0.385 of the rate (17 kHz: 0.385 * 2^18) read at ie (the phase
 * increment >> 14, times how fast the cycle is read, Q8); not built yet: the next one down that is, else up; -1 none */
static int32_t wv_level(const wv_part_t *P, uint32_t o, uint32_t ie)
{
    uint32_t L, k;
    for (L = 0; L < WV_NL - 1u && (ie >> 8) * (WV_H0 >> L) > 100925u; L++)
        ;
    if ((P->ready[o] >> L) & 1u)
        return (int32_t)L;
    for (k = L; k < WV_NL && !((P->ready[o] >> k) & 1u); k++)
        ;
    if (k >= WV_NL)
        for (k = L; k > 0u && !((P->ready[o] >> k) & 1u); k--)
            ;
    return (P->ready[o] >> k) & 1u ? (int32_t)k : -1;
}
/* POS (0 .. 127 << 8) in a table: the frame before it (*f0), how far to the next one (/ 256) */
static int32_t wv_frame(int32_t posq, uint32_t *f0)
{
    int32_t fpos = posq * (int32_t)(WV_NF - 1u) / 127, pf = fpos & 255;   /* 0 .. 5 << 8 */
    *f0 = (uint32_t)fpos >> 8;
    if (*f0 >= WV_NF - 1u) {
        *f0 = WV_NF - 2u;
        pf = 256;
    }
    return pf;
}

/* an oscillator for a block: where it reads, how its WARP maps the cycle, its pitch and level */
typedef struct {
    const int16_t *a, *b;        /* the two frames beside POS, at the level for its pitch; 0: silent */
    uint32_t sh, mask;
    int32_t pf;                  /* / 256 of the way from a to b */
    uint32_t inc, dinc;          /* a sample's step; THICK's copies: that much more and less (0: one copy) */
    int32_t g;                   /* LEVEL, Q15 x 1.5 */
    uint32_t kind;               /* WK_*: OFF at amount 0 */
    uint32_t knee, k1, k2;       /* BEND: the cycle's first half up to knee (k1 its speed, Q8), the second after it
                                  * (k2); PWM: the whole cycle up to knee, silence after; SYNC: k1 the speed */
    int32_t fm;                  /* FM: the amount */
} wv_osc_t;
static inline uint32_t wv_mul(uint32_t x, uint32_t k)     /* x k / 256, in 32 bits (it wraps as a phase does) */
{
    return (((x >> 16) * k) << 8) + (((x & 0xFFFFu) * k) >> 8);
}
/* a WARP's kind and amount as a map of the cycle; how fast it reads the cycle at most, Q8 (the level to play) */
static uint32_t wv_warp_set(wv_osc_t *O, uint32_t kind, int32_t amt)
{
    uint32_t f;
    O->kind = amt > 0 ? kind : WK_OFF;
    O->knee = O->k2 = 0;
    O->k1 = 256u;
    O->fm = amt;
    switch (O->kind) {
    case WK_SYNC:                                     /* 1x .. 8.4x */
        O->k1 = 256u + (uint32_t)amt * 15u;
        return O->k1;
    case WK_BENDP:                                    /* the middle of the cycle at 50 % .. 5 % of it, or (BEND-) .. 95 % */
    case WK_BENDM:
        f = O->kind == WK_BENDP ? 32768u - (uint32_t)amt * 232u : 32768u + (uint32_t)amt * 232u;
        O->knee = f << 16;
        O->k1 = 8388608u / f;
        O->k2 = 8388608u / (65536u - f);
        return O->k1 > O->k2 ? O->k1 : O->k2;
    case WK_PWM:                                      /* the cycle in 100 % .. 10 % of its time */
        f = 65536u - (uint32_t)amt * 464u;
        O->knee = f << 16;
        O->k1 = 16777216u / f;
        return O->k1;
    case WK_FM:
        return 256u + (uint32_t)amt * 6u;             /* (its sidebands: a level lower as it deepens) */
    default:
        return 256u;
    }
}
/* where in the cycle phase ph reads; *w: the sample's weight / 32768 (SYNC's window, PWM's silence) */
static inline uint32_t wv_map(const wv_osc_t *O, uint32_t ph, int32_t *w)
{
    uint32_t d;
    switch (O->kind) {
    case WK_SYNC:                                     /* the cycle read k1 times as fast, closed at its ends */
        d = (ph < 0x80000000u ? ph : ~ph) >> 13;
        *w = (int32_t)(d > 32767u ? 32767u : d);
        return wv_mul(ph, O->k1);
    case WK_BENDP:
    case WK_BENDM:
        return ph < O->knee ? wv_mul(ph, O->k1) : 0x80000000u + wv_mul(ph - O->knee, O->k2);
    case WK_PWM:
        if (ph >= O->knee) {
            *w = 0;
            return 0;
        }
        return wv_mul(ph, O->k1);
    default:
        return ph;
    }
}
static inline int32_t wv_osc(const wv_osc_t *O, uint32_t ph, int32_t other)
{
    int32_t w = 32767, s;
    if (O->kind == WK_FM)
        ph += (uint32_t)(other * O->fm) << 10;        /* +- half a cycle at the other's peaks, all the way */
    else if (O->kind)
        ph = wv_map(O, ph, &w);
    s = wv_read(O->a, O->b, ph, O->sh, O->mask, O->pf);
    return O->kind ? (s * w) >> 15 : s;
}
/* .. with THICK's two copies, phi before and after it */
static inline int32_t wv_voice(const wv_osc_t *O, uint32_t ph, uint32_t phi, int32_t other)
{
    int32_t s = wv_osc(O, ph, other);
    if (O->dinc)
        s = ((s + wv_osc(O, ph + phi, other) + wv_osc(O, ph - phi, other)) * 19661) >> 15;
    return s;
}
/* oscillator o of the sound p for a block: its table at POS posq, read at inc a sample; 0: silent (LEVEL 0, no table yet) */
static int wv_setup(wv_osc_t *O, const wv_part_t *P, const int16_t *p, uint32_t o, uint32_t inc, int32_t posq)
{
    int32_t lv = wv_x(p, WX_ALVL + o), L, th = wv_x(p, WX_ATHICK + o);
    uint32_t f0, speed = wv_warp_set(O, wv_kind(p, o), wv_amt(p, o));
    O->a = O->b = 0;
    O->inc = inc;
    O->dinc = th ? (inc >> 12) * (uint32_t)((th * 121) >> 7) : 0u;
    O->g = (lv * lv * 3) + ((lv * lv * 3) >> 6) / 2;    /* Q15 x 1.5: 127 -> 48765 */
    if (!lv || (L = wv_level(P, o, (inc >> 14) * speed)) < 0)
        return 0;
    O->pf = wv_frame(posq, &f0);
    O->sh = 24u + (uint32_t)L;
    O->mask = (256u >> L) - 1u;
    O->a = &P->tab[o][f0][WV_OFF[L]];
    O->b = &P->tab[o][f0 + 1u][WV_OFF[L]];
    return 1;
}
static inline int32_t wv_sub_at(uint32_t kind, uint32_t ph)
{
    int32_t s;
    if (kind == WS_TRI) {
        uint32_t t = ph >> 15;                        /* 0 .. 131071 */
        return t < 65536u ? (int32_t)t - 32768 : 98303 - (int32_t)t;
    }
    s = sine_i(ph);
    return kind == WS_SQUARE ? softclip(s * 6) : s;
}
static void wave_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    wv_part_t *P = wv_of(t);
    wv_osc_t A, B;
    uint32_t i, ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2], fa = (uint32_t)v->s[6], fb = (uint32_t)v->s[7];
    uint32_t through = wv_through(p), fk = wv_fkind(p), sk = wv_subk(p), sinc = m->inc >> (sk == WS_DEEP ? 2 : 1);
    uint32_t nz = (uint32_t)v->s[4];
    int32_t ca = wv_cents(p, 0), cb = wv_cents(p, 1), posq, on_a, on_b, bfirst;
    int32_t subg = clamp(p[P_E3], 0, 127) * 180, nl = wv_x(p, WX_NLVL), ng = nl * nl * 2, nc = wv_x(p, WX_NCOL);
    int32_t ncoef = clamp(600 + nc * nc * 2, 0, 32767), nlp = v->s[5];
    int32_t drv = clamp(p[P_E7], 0, 127), drive = 32768 + drv * 512;
    int32_t cut = (p[P_E4] << 8) + m->cutoff + ((wv_x(p, WX_KTRK) * (v->pitch16 - 60 * 16)) >> 4);
    int32_t kq = 8192 - clamp(p[P_E5], 0, 127) * 7600 / 127;
    int32_t ic1 = v->s[0], ic2 = v->s[1], ic3 = v->s[2], ic4 = v->s[3];
    tsvf_t flt, flt2;
    if (!P || !P->live)
        return;
    posq = clamp((p[P_E1] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    P->pos[0] = (uint16_t)posq;
    on_a = wv_setup(&A, P, p, 0, ca ? cents_inc(m->pitch16, ca, m->fine) : m->inc, posq);
    posq = clamp((p[P_E2] << 8) + m->shape - (64 << 8), 0, 127 << 8);
    P->pos[1] = (uint16_t)posq;
    on_b = wv_setup(&B, P, p, 1, cb ? cents_inc(m->pitch16, cb, m->fine) : m->inc, posq);
    if (!on_a && !on_b && !subg && !ng)
        return;
    if (A.kind == WK_FM && B.kind == WK_FM)           /* (each by the other: B plays plain) */
        B.kind = WK_OFF;
    bfirst = A.kind == WK_FM;
    tsvf_coef_k(&flt, cut, kq);
    tsvf_coef_k(&flt2, cut, 5793);                    /* LOW 24's second stage: no peak of its own */
    for (i = 0; i < n; i++) {
        int32_t sa = 0, sb = 0, ss = 0, sn = 0, sf = 0, sd = 0, v1, v2, v3, y;
        if (bfirst) {
            if (on_b)
                sb = wv_voice(&B, ph1, fb, 0);
            if (on_a)
                sa = wv_voice(&A, ph0, fa, sb);
        } else {
            if (on_a)
                sa = wv_voice(&A, ph0, fa, 0);
            if (on_b)
                sb = wv_voice(&B, ph1, fb, sa);
        }
        ph0 += A.inc;
        ph1 += B.inc;
        fa += A.dinc;
        fb += B.dinc;
        sa = (sa * A.g) >> 15;
        sb = (sb * B.g) >> 15;
        if (subg) {
            ss = (wv_sub_at(sk, ph2) * subg) >> 15;
            ph2 += sinc;
        }
        if (ng) {
            nz ^= nz << 13;
            nz ^= nz >> 17;
            nz ^= nz << 5;
            nlp += ((((int32_t)(int16_t)(nz >> 8)) - nlp) * ncoef) >> 15;
            sn = (nlp * ng) >> 15;
        }
        if (through & WR_A) sf += sa; else sd += sa;
        if (through & WR_B) sf += sb; else sd += sb;
        if (through & WR_NOISE) sf += sn; else sd += sn;
        if (through & WR_SUB) sf += ss; else sd += ss;
        if (drv)
            sf = softclip(((sf >> 2) * (drive >> 2)) >> 11);
        sf >>= 1;
        v3 = sf - ic2;
        v1 = (flt.a1 * ic1 + flt.a2 * v3) >> 13;
        v2 = ic2 + ((flt.a2 * ic1 + flt.a3 * v3) >> 13);
        ic1 = clamp(2 * v1 - ic1, -150000, 150000);
        ic2 = clamp(2 * v2 - ic2, -150000, 150000);
        y = fk == WF_BAND ? v1 : fk == WF_HIGH ? sf - ((kq * (v1 >> 2)) >> 10) - v2 :
            fk == WF_NOTCH ? sf - ((kq * (v1 >> 2)) >> 10) : v2;
        if (fk == WF_LOW24)
            y = tsvf_lp(&flt2, y, &ic3, &ic4);
        out[i] += voice_amp2(soft_knee(y + (sd >> 1), 16000) << 1, m, i);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = ic3;
    v->s[3] = ic4;
    v->s[4] = (int32_t)nz;
    v->s[5] = nlp;
    v->s[6] = (int32_t)fa;
    v->s[7] = (int32_t)fb;
}

/* the factory sounds. E: TABLES, A POS, B POS, SUB, CUTOFF, RES, A's WARP amount, DRIVE; then ATK DEC SUS REL, the
 * filter's ENV amount, MONO; the rest of each in WV_PX, its row */
static const preset_t WAVE_PRESETS[] = {
    {"INIT SAW", {WVT(0, 0), 127, 127, 0, 127, 0, 0, 0}, {0, 64, 127, 20}, 0, 0, FX(0, 0, 0, 0), TAG(PT_LEAD)},
    {"SUPERSAW", {WVT(0, 0), 127, 127, 0, 112, 8, 0, 0}, {6, 80, 112, 62}, 0, 0, FX(0, 90, 25, 45), TAG(PT_PAD)},
    {"WT GROWL", {WVT(4, 4), 24, 24, 59, 74, 28, 0, 54}, {0, 70, 112, 24}, 18, 1, FX(18, 0, 0, 5),
     MORE({P_LRATE, 74}, {P_LD_SHP, 52}, {P_GLIDE, 12}), TAG(PT_BASS)},
    {"WOBBLE", {WVT(3, 3), 44, 44, 85, 66, 36, 0, 36}, {0, 70, 120, 22}, 0, 1, FX(10, 0, 0, 5),
     MORE({P_LWAVE, 1}, {P_LRATE, 66}, {P_LD_SHP, 63}), TAG(PT_BASS)},
    {"TALK BASS", {WVT(3, 3), 8, 8, 51, 78, 30, 0, 36}, {0, 62, 100, 20}, 0, 1, FX(8, 0, 5, 5),
     MORE({P_ED_SHP, 56}), TAG(PT_BASS)},
    {"REESE WT", {WVT(0, 0), 112, 112, 42, 56, 18, 0, 18}, {0, 64, 127, 30}, 8, 1, FX(8, 20, 0, 8),
     MORE({P_LRATE, 30}, {P_LD_FLT, 8}), TAG(PT_BASS)},
    {"SUB WT", {WVT(0, 0), 22, 22, 102, 72, 0, 0, 18}, {0, 62, 110, 20}, 0, 1, FX(0, 0, 0, 5), TAG(PT_BASS)},
    {"PWM STRINGS", {WVT(2, 2), 52, 52, 0, 92, 8, 0, 0}, {58, 82, 116, 82}, 0, 0, FX(0, 80, 15, 60),
     MORE({P_LWAVE, 1}, {P_LRATE, 44}, {P_LD_SHP, 40}), TAG(PT_PAD)},
    {"VOX PAD", {WVT(3, 0), 30, 46, 0, 84, 12, 0, 0}, {72, 90, 112, 92}, 0, 0, FX(0, 60, 20, 72),
     MORE({P_LRATE, 28}, {P_LD_SHP, 46}), TAG(PT_PAD)},
    {"DIGI SWEEP", {WVT(5, 5), 0, 0, 0, 104, 10, 0, 0}, {0, 88, 60, 62}, 0, 0, FX(0, 30, 45, 40),
     MORE({P_ED_SHP, 62}), TAG(PT_KEYS)},
    {"WT BELL", {WVT(6, 6), 96, 96, 0, 127, 0, 0, 0}, {0, 96, 0, 84}, 0, 0, FX(0, 20, 30, 62),
     MORE({P_ED_SHP, -44}), TAG(PT_KEYS)},
    {"WT PLUCK", {WVT(0, 0), 40, 40, 0, 62, 18, 0, 0}, {0, 72, 0, 52}, 42, 0, FX(0, 20, 45, 35),
     MORE({P_ED_SHP, 54}), TAG(PT_PLUCK)},
    {"WARP LEAD", {WVT(0, 0), 127, 127, 0, 104, 18, 55, 18}, {2, 70, 104, 40}, 0, 1, FX(0, 15, 45, 30),
     MORE({P_GLIDE, 22}, {P_LRATE, 86}, {P_LFADE, 90}, {P_LD_PIT, 8}), TAG(PT_LEAD)},
    {"HOLLOW LEAD", {WVT(1, 1), 104, 104, 0, 98, 22, 0, 0}, {3, 70, 108, 44}, 10, 1, FX(0, 20, 40, 35),
     MORE({P_GLIDE, 18}, {P_LRATE, 84}, {P_LFADE, 92}, {P_LD_PIT, 8}), TAG(PT_LEAD)},
    {"ORGAN WT", {WVT(7, 1), 92, 30, 17, 122, 0, 0, 0}, {2, 64, 127, 18}, 0, 0, FX(0, 60, 10, 30), TAG(PT_KEYS)},
    {"FIFTHS", {WVT(0, 2), 96, 88, 0, 100, 10, 0, 0}, {2, 70, 110, 36}, 0, 0, FX(0, 20, 35, 30), TAG(PT_LEAD)},
};
#define WVP_OFF WVA(WK_OFF, 0)
#define WVF_ALL WVF(WF_LOW, WR_ALL)
/*   A LVL  B LVL  A WARP,OCT          B WARP,OCT          SEMI    FINE     THICK    B AMT SUB      NOISE   FILTER   KEY */
static const uint8_t WV_PX[][WX_N] = {
    {127,   0, WVP_OFF,            WVP_OFF,            12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* INIT SAW */
    {105, 105, WVP_OFF,            WVP_OFF,            12, 12, 64, 76,  60, 90,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* SUPERSAW */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* WT GROWL */
    {127,   0, WVP_OFF,            WVP_OFF,            12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* WOBBLE */
    {127,   0, WVP_OFF,            WVP_OFF,            12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* TALK BASS */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 86,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* REESE WT */
    {127,   0, WVP_OFF,            WVP_OFF,            12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* SUB WT */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,  30,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* PWM STRINGS */
    {117,  91, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* VOX PAD */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* DIGI SWEEP */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* WT BELL */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* WT PLUCK */
    {127,   0, WVA(WK_SYNC, 0),    WVP_OFF,            12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* WARP LEAD */
    {107, 107, WVP_OFF,            WVP_OFF,            12, 12, 64, 72,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* HOLLOW LEAD */
    {119,  87, WVP_OFF,            WVA(WK_OFF, 1),     12, 12, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* ORGAN WT */
    {111, 102, WVP_OFF,            WVP_OFF,            12, 19, 64, 64,   0,  0,  0, WS_SINE,   0, 127, WVF_ALL, 64},   /* FIFTHS */
};
_Static_assert(NELEM(WV_PX) == NELEM(WAVE_PRESETS), "WAVE: a row of WV_PX a factory sound");
/* factory sound pi's sixteen (ui.c preset_fill: after the common values' defaults) */
static void wave_px(int16_t *p, uint32_t pi)
{
    uint32_t k;
    for (k = 0; k < WX_N; k++)
        p[WX(k)] = WV_PX[pi % NELEM(WV_PX)][k];
}

static const engine_t ENG_WAVE = {
    .name = "WAVE",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"TABLES", 0, 0, 63, 0, F_INT, 0},           /* (Skiff's own pages: the eight as stored; the designer's pages: */
        {"A POS", 0, 0, 127, 127, F_PCT, 0},         /* every control, ui_design.c) */
        {"B POS", 0, 0, 127, 127, F_PCT, 0},
        {"SUB", 0, 0, 127, 0, F_PCT, 0},
        {"CUTOFF", 0, 0, 127, 127, F_CUTOFF, 0},
        {"RES", 0, 0, 127, 0, F_PCT, 0},
        {"WARP A", 0, 0, 127, 0, F_PCT, 0},
        {"DRIVE", 0, 0, 127, 0, F_PCT, 0},
    },
    .presets = WAVE_PRESETS,
    .npresets = NELEM(WAVE_PRESETS),
    .note_on = wave_note_on,
    .render = wave_render,
    .block = wave_block,
    .knob = {P_E1, P_E2, P_E4, P_E5},
    .poly = 8,
    .keep = 0x0F,                /* the filter */
};
