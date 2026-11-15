/* SPDX-License-Identifier: GPL-3.0-only
 * TAPE (SEQ > TAPE): what is played, kept as it was played. A note is its track, its pitch, its velocity, when it
 * started (in samples from the tape's start) and how long it was held: no steps, no length set beforehand, any
 * number of notes at once. One tape for the six tracks: record a track, pick another, record over it (the first
 * plays meanwhile), and so on. The tape's end is where the first take stopped; a later take that runs on past it
 * moves it. LOOP: at the end, round again (and what is recorded then joins what is there).
 *
 * The tape runs with the transport (seq_start / seq_stop): PLAY plays it with the patterns, from its start.
 * Recording (tape.rec) takes what take_note is given: the keys and their chords, MIDI IN, the arp's notes. Armed
 * while stopped, the first note starts the transport, at the tape's start.
 *
 * The notes sit in one pool, appended as they are played. A RUN is a stretch of the pool in the order of time: a
 * take is one run, and with LOOP on every pass of it that got a note is one more. Playing walks each run with a
 * cursor (the cost of a block: the runs, not the notes). The run being recorded is skipped (it sounds as played)
 * until its pass is over.
 *
 * Who touches what: tape_block and tape_rec_note run in the audio ISR and own the pool, the runs and the lists.
 * The main loop asks through the q_* bytes (each written whole), reads for the screen, and may reorder the pool
 * only with the transport stopped and tape.busy set (tape_tidy), which the ISR respects.
 * Included by seq.c before take.c (take_note hands the notes over). */
#ifndef TAPE_NOTES
#define TAPE_NOTES 950u                  /* the pool: 8 bytes a note (as many as two flash chunks keep: tape_store.c) */
#endif
#define TAPE_RUNS 32u
#define TAPE_ACT 24u                     /* the tape's notes sounding */
#define TAPE_OPEN 16u                    /* notes being recorded, still held */
#define TAPE_LEN_U 128u                  /* a note's length is kept in units of 128 samples (2.9 ms: a half buffer) */
#define TAPE_POS_MAX 0x0FFFFFFFu         /* a start is 28 bits of samples: 101 minutes */
#define TAPE_DEL 0x80000000u             /* st: cleared (it never plays; an undo brings it back until it is purged) */
#define TAPE_GONE 0x80u                  /* vel: cleared for good (a take began since): tape_tidy removes it */
#define TAPE_MIN_LEN 4410u               /* the shortest tape END's knob leaves: 0.1 s */
enum { TU_NONE, TU_TAKE, TU_CLEAR };     /* what UNDO takes back */
enum { TF_NONE, TF_NOTES, TF_RUNS, TF_TIME };   /* why a take stopped taking notes */

typedef struct { uint32_t st; uint16_t len; uint8_t note, vel; } tape_note_t;   /* st: start | track << 28 | TAPE_DEL */
typedef struct { uint16_t first, n, cur; } tape_run_t;
typedef struct { uint32_t left; uint8_t trk, note; } tape_act_t;
typedef struct { uint32_t held; uint16_t idx; uint8_t trk, note; } tape_open_t;

static tape_note_t tape_nt[TAPE_NOTES];   /* (.bss, zeroed at power-on as the pool is: the pool's 8 KiB spare stays
                                             * what it was, and .bss has the room: tools/build.py's two limits) */
static tape_run_t tape_rn[TAPE_RUNS] __attribute__((section(".pool")));
static tape_act_t tape_ac[TAPE_ACT] __attribute__((section(".pool")));
static tape_open_t tape_op[TAPE_OPEN] __attribute__((section(".pool")));
static struct {
    volatile uint32_t pos;               /* samples since the tape's start */
    volatile uint32_t len;               /* its end; 0: none yet (the first take sets it) */
    volatile uint16_t n;                 /* notes in the pool (cleared ones too, until tape_tidy) */
    volatile uint8_t nrun;
    volatile uint8_t run;                /* rolling (the transport plays) */
    volatile uint8_t rec;                /* recording */
    volatile uint8_t loop;
    volatile uint8_t busy;               /* the main loop reorders the pool: the ISR keeps off */
    volatile uint8_t full;               /* TF_*: the take could take no more */
    volatile uint8_t back;               /* TU_*: what UNDO takes back */
    volatile uint8_t gen;                /* counts every change of the notes (the screen's signature) */
    uint8_t rrun;                        /* 1 + the run being recorded this pass; 0: none yet */
    uint8_t pre, lead;                   /* a take's first note came while stopped (pre), blk0 blocks before the
                                          * transport started: the tape's first block is that many blocks longer (lead),
                                          * so what follows keeps its distance from that first note */
    uint32_t blk, blk0;                  /* blocks since power-on; the block of that first note */
    uint8_t nact, nopen;
    uint8_t u_mask;                      /* TU_CLEAR: the tracks cleared */
    uint16_t t_n0;                       /* TU_TAKE: the pool, the runs and the end before the take */
    uint8_t t_run0;
    uint32_t t_len0, u_len;              /* (u_len: TU_CLEAR's, when the clear emptied the tape) */
    volatile uint8_t q_arm, q_disarm, q_undo, q_clr;   /* the main loop's asks (q_clr: a mask of tracks) */
    uint8_t to_pat;                      /* REC TO: 0 TAPE (REC records onto the tape on every page, HOME shows it:
                                          * power-on), 1 PATTERN (Skiff's own recording and HOME, as they were) */
    uint8_t ended;                       /* it played to its end with LOOP off: the stop was asked, once */
    uint8_t ugen;                        /* counts the takes and wipes that can be undone (ui_tape.c: which undo is the newest) */
    uint8_t q_retake;                    /* 1 + a track: its tape wiped and a take armed, once it has stopped (ui_tape.c) */
} tape __attribute__((section(".pool")));
/* the layout: HOME is the tape's screen, REC records onto it there (the steps on SEQ's pages), SAVE and EDIT as
 * ui_save2.c and ui_design.c have them. to_pat 1: Skiff's own layout, as its tests know it (the unit: main.c
 * tape_power_on) */
#ifndef FELUCCA_NEWUI
#define FELUCCA_NEWUI 0              /* 1: this layout only (the unit's build, the browser's): Skiff's own, which its
                                      * host tests run on, is left out of the code (what only it reaches is not built) */
#endif
#if FELUCCA_NEWUI
#define UI_NEW 1
#else
#define UI_NEW (!tape.to_pat)
#endif
/* a page or a layer that only Skiff's own layout has: never there in the new layout's own build (and not built) */
#define OLD_UI(x) (!FELUCCA_NEWUI && (x))

static uint32_t tape_trk_of(const tape_note_t *e) { return (e->st >> 28) & 7u; }
static uint32_t tape_st_of(const tape_note_t *e) { return e->st & TAPE_POS_MAX; }
static int tape_live(const tape_note_t *e) { return !(e->st & TAPE_DEL); }

/* the notes of track k on the tape (k >= NTRK: of all); main loop, for the screen */
static uint32_t tape_count(uint32_t k)
{
    uint32_t i, c = 0, n = tape.n;
    for (i = 0; i < n; i++)
        c += tape_live(&tape_nt[i]) && (k >= NTRK || tape_trk_of(&tape_nt[i]) == k);
    return c;
}

/* the notes of every track at once (the screens: HOME's lanes, the tape's page), counted again only when the notes
 * changed. c: NTRK counts. Main loop */
static void tape_counts(uint16_t *c)
{
    static uint16_t cnt[NTRK] __attribute__((section(".pool")));
    static uint8_t at __attribute__((section(".pool"))), have __attribute__((section(".pool")));
    uint32_t i, n = tape.n, g = tape.gen;
    if (!have || at != (uint8_t)g) {
        for (i = 0; i < NTRK; i++)
            cnt[i] = 0;
        for (i = 0; i < n; i++)
            if (tape_live(&tape_nt[i]))
                cnt[tape_trk_of(&tape_nt[i]) % NTRK]++;
        at = (uint8_t)g;
        have = 1;
    }
    for (i = 0; i < NTRK; i++)
        c[i] = cnt[i];
}

/* ---- the audio ISR's side ---- */
static void tape_silence(uint32_t mask)                 /* the tape's sounding notes on the tracks of mask end */
{
    uint32_t i = 0;
    while (i < tape.nact) {
        if ((mask >> tape_ac[i].trk) & 1u) {
            trk_note_off(&trk[tape_ac[i].trk % NTRK], tape_ac[i].note);
            tape_ac[i] = tape_ac[--tape.nact];
        } else
            i++;
    }
}

static void tape_sound(uint32_t k, uint32_t note, uint32_t vel, uint32_t left)
{
    uint32_t i;
    for (i = 0; i < tape.nact; i++)
        if (tape_ac[i].trk == k && tape_ac[i].note == note)
            break;                                      /* (struck again: one end, the later) */
    if (i == tape.nact) {
        if (tape.nact == TAPE_ACT) {                    /* (full: the first one ends early) */
            trk_note_off(&trk[tape_ac[0].trk % NTRK], tape_ac[0].note);
            tape_ac[0] = tape_ac[--tape.nact];
            i = tape.nact;
        }
        tape.nact++;
    }
    tape_ac[i].trk = (uint8_t)k;
    tape_ac[i].note = (uint8_t)note;
    tape_ac[i].left = left;
    trk_note_on(&trk[k % NTRK], note, vel);
}

static void tape_cursors(void)                          /* every run from its first note */
{
    uint32_t r;
    for (r = 0; r < tape.nrun; r++)
        tape_rn[r].cur = 0;
}

static void tape_close(uint32_t i)                      /* a recorded note let go: its length */
{
    uint32_t u = (tape_op[i].held + TAPE_LEN_U / 2u) / TAPE_LEN_U;
    tape_nt[tape_op[i].idx % TAPE_NOTES].len = (uint16_t)(u < 1u ? 1u : u > 0xFFFFu ? 0xFFFFu : u);
    tape_op[i] = tape_op[--tape.nopen];
}

static void tape_take_begin(void)
{
    uint32_t i, n = tape.n;
    if (tape.back != TU_CLEAR) {                        /* what was cleared before is gone for good (a wipe just made
                                                         * can still be undone until this take is kept: tape_take_end) */
        for (i = 0; i < n; i++)
            if (!tape_live(&tape_nt[i]))
                tape_nt[i].vel |= TAPE_GONE;
        tape.back = TU_NONE;
    }
    tape.t_n0 = tape.n;
    tape.t_run0 = tape.nrun;
    tape.t_len0 = tape.len;
    tape.rrun = 0;
    tape.nopen = 0;
    tape.pre = tape.lead = 0;
    if (!tape.run)
        tape.pos = 0;                                   /* (stopped: from the tape's start) */
    tape.full = TF_NONE;
    tape.rec = 1;
}

static void tape_take_end(void)
{
    while (tape.nopen)
        tape_close(0);
    tape.rec = 0;
    tape.rrun = 0;
    tape.pre = tape.lead = 0;
    if (tape.n > tape.t_n0) {                           /* it holds notes: one undo; the tape's end */
        if (!tape.len || (!tape.loop && tape.pos > tape.len))
            tape.len = tape.pos > TAPE_MIN_LEN ? tape.pos : TAPE_MIN_LEN;
        {
            uint32_t i;
            for (i = 0; i < tape.t_n0; i++)             /* (the take is kept: a wipe before it cannot come back) */
                if (!tape_live(&tape_nt[i]))
                    tape_nt[i].vel |= TAPE_GONE;
        }
        tape.back = TU_TAKE;
        tape.ugen++;
        tape.gen++;
    }
}

static void tape_requests(void)
{
    uint32_t i, m, n = tape.n, live = 0;
    if (tape.q_clr) {
        m = tape.q_clr;
        tape.q_clr = 0;
        if (tape.rec)
            tape_take_end();
        for (i = 0; i < n; i++) {
            if (tape_live(&tape_nt[i]) && ((m >> tape_trk_of(&tape_nt[i])) & 1u))
                tape_nt[i].st |= TAPE_DEL;
            live += tape_live(&tape_nt[i]);
        }
        tape_silence(m);
        tape.back = TU_CLEAR;
        tape.ugen++;
        tape.u_mask = (uint8_t)m;
        tape.u_len = tape.len;
        if (!live)
            tape.len = 0;                               /* an empty tape: its end is the next first take's */
        tape.gen++;
    }
    if (tape.q_undo) {
        tape.q_undo = 0;
        if (tape.rec)
            tape_take_end();
        if (tape.back == TU_TAKE) {
            tape_silence(0xFFu);
            tape.n = tape.t_n0;
            tape.nrun = tape.t_run0;
            tape.len = tape.t_len0;
            for (i = 0; i < tape.nrun; i++)
                if (tape_rn[i].cur > tape_rn[i].n)
                    tape_rn[i].cur = tape_rn[i].n;
        } else if (tape.back == TU_CLEAR) {
            for (i = 0; i < n; i++)
                if (!tape_live(&tape_nt[i]) && !(tape_nt[i].vel & TAPE_GONE) &&
                    ((tape.u_mask >> tape_trk_of(&tape_nt[i])) & 1u))
                    tape_nt[i].st &= ~TAPE_DEL;
            tape.len = tape.u_len;
        }
        tape.back = TU_NONE;
        tape.gen++;
    }
    if (tape.q_arm) {
        tape.q_arm = 0;
        if (!tape.rec)
            tape_take_begin();
    }
    if (tape.q_disarm) {
        tape.q_disarm = 0;
        if (tape.rec)
            tape_take_end();
    }
}

/* the transport starts (seq_start): the tape from its start. A take armed while stopped goes on: its first notes
 * are at the start already */
static void tape_start(void)
{
    if (tape.busy) {
        tape.run = 0;
        return;
    }
    tape_silence(0xFFu);
    tape.pos = 0;
    tape.ended = 0;
    tape.lead = 0;
    if (tape.pre) {                                     /* (the key came a block or so before this start) */
        uint32_t d = tape.blk - tape.blk0;
        tape.lead = (uint8_t)(d < 8u ? d : 8u);
        tape.pre = 0;
    }
    tape_cursors();
    if (tape.rrun)
        tape_rn[(tape.rrun - 1u) % TAPE_RUNS].cur = tape_rn[(tape.rrun - 1u) % TAPE_RUNS].n;
    tape.run = 1;
}

/* the transport stops (seq_stop): its notes end, a take ends */
static void tape_stop(void)
{
    if (tape.busy)
        return;
    tape_silence(0xFFu);
    if (tape.rec)
        tape_take_end();
    tape.run = 0;
    tape.pos = 0;                                       /* (rewound: the next start is from its start, and the screens
                                                         * show it so) */
}

/* a note on (vel > 0) or off played on track t (take_note's callers). 1: the tape took it */
static __attribute__((noinline)) int tape_rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t k = trk_index(t), i;
    tape_note_t *e;
    if (!tape.rec || tape.busy)
        return 0;
    if (!vel) {
        for (i = 0; i < tape.nopen; i++)
            if (tape_op[i].trk == k && tape_op[i].note == note) {
                tape_close(i);
                tape.gen++;
                break;
            }
        return 1;
    }
    if (!tape.run && !tape.pre) {                       /* armed, stopped: the first note starts it */
        tape.pre = 1;
        tape.blk0 = tape.blk;
        if (!song.playing && !transport_req)
            transport_req = 1;
    }
    if (tape.full)
        return 1;
    if (tape.n >= TAPE_NOTES) {
        tape.full = TF_NOTES;
        return 1;
    }
    if (tape.run && tape.pos >= TAPE_POS_MAX) {
        tape.full = TF_TIME;
        return 1;
    }
    if (!tape.rrun) {                                   /* this pass's run */
        if (tape.nrun >= TAPE_RUNS) {
            tape.full = TF_RUNS;
            return 1;
        }
        tape_rn[tape.nrun].first = tape.n;
        tape_rn[tape.nrun].n = 0;
        tape_rn[tape.nrun].cur = 0;
        tape.rrun = (uint8_t)(tape.nrun + 1u);
        RING_PUBLISH();
        tape.nrun++;
    }
    for (i = 0; i < tape.nopen; i++)                    /* (the same key again without its release: that one ends) */
        if (tape_op[i].trk == k && tape_op[i].note == note) {
            tape_close(i);
            break;
        }
    if (tape.nopen == TAPE_OPEN)
        tape_close(0);
    e = &tape_nt[tape.n];
    e->st = (tape.run ? tape.pos : 0u) | k << 28;
    e->len = 0;                                         /* (held: no length yet; it does not play until it has one) */
    e->note = (uint8_t)(note & 127u);
    e->vel = (uint8_t)(vel & 127u);
    tape_op[tape.nopen].held = 0;
    tape_op[tape.nopen].idx = tape.n;
    tape_op[tape.nopen].trk = (uint8_t)k;
    tape_op[tape.nopen].note = (uint8_t)note;
    tape.nopen++;
    tape_rn[tape.rrun - 1u].n++;
    tape_rn[tape.rrun - 1u].cur = tape_rn[tape.rrun - 1u].n;   /* (behind the playhead: not played again this pass) */
    RING_PUBLISH();
    tape.n++;
    tape.gen++;
    return 1;
}

/* the notes that start before `to` sound */
static void tape_span(uint32_t to)
{
    uint32_t r;
    for (r = 0; r < tape.nrun; r++) {
        tape_run_t *q = &tape_rn[r];
        if (r + 1u == tape.rrun)
            continue;
        while (q->cur < q->n) {
            const tape_note_t *e = &tape_nt[(q->first + q->cur) % TAPE_NOTES];
            if (tape_st_of(e) >= to)
                break;
            q->cur++;
            if (tape_live(e) && e->len)
                tape_sound(tape_trk_of(e), e->note, e->vel & 127u, (uint32_t)e->len * TAPE_LEN_U);
        }
    }
}

/* a block of n samples (events_block, every block). Not inlined: its loops stay out of events_block's own cost
 * (tests/target_budget.py), as take_note's and the launch's do */
static __attribute__((noinline)) void tape_block(uint32_t n)
{
    uint32_t i, left = n, seg;
    tape.blk++;
    if (tape.busy)
        return;
    tape_requests();
    if (!tape.run)
        return;
    if (tape.lead) {
        left += tape.lead * n;
        tape.lead = 0;
    }
    for (i = 0; i < tape.nopen; i++)
        if (tape_op[i].held < 0xFFFFu * TAPE_LEN_U)
            tape_op[i].held += left;
    for (i = 0; i < tape.nact;) {                       /* the ends first: a note struck again starts after */
        if (tape_ac[i].left <= n) {
            trk_note_off(&trk[tape_ac[i].trk % NTRK], tape_ac[i].note);
            tape_ac[i] = tape_ac[--tape.nact];
        } else
            tape_ac[i++].left -= n;
    }
    while (left) {
        if (tape.loop && tape.len && tape.pos >= tape.len) {   /* the end: round again */
            tape.pos = 0;
            tape_cursors();
            tape.rrun = 0;                              /* (what this pass recorded plays from the next) */
        }
        seg = left;
        if (tape.loop && tape.len && tape.pos + seg > tape.len)
            seg = tape.len - tape.pos;
        if (tape.pos >= TAPE_POS_MAX)
            break;                                      /* (101 minutes: it stands) */
        tape_span(tape.pos + seg);
        tape.pos += seg;
        left -= seg;
    }
    if (!tape.loop && !tape.rec && tape.len && tape.pos >= tape.len && !tape.ended) {
        tape.ended = 1;                                 /* played to its end, LOOP off: that is the song. Stop (a take
                                                         * being recorded runs on, and moves the end) */
        if (!transport_req)
            transport_req = 2;
    }
}

/* ---- the main loop's side ---- */
/* the pool put in order, with the transport stopped: the cleared notes out, the runs made one when they are many.
 * A clear can no longer be undone after it (its notes are gone) */
static void tape_tidy(void)
{
    uint32_t r, i, w = 0, nr = 0, first, kept;
    if (song.playing || transport_req || tape.rec || tape.q_arm)
        return;
    tape.busy = 1;
    tape.nact = tape.nopen = 0;
    for (r = 0; r < tape.nrun; r++) {
        first = w;
        for (i = 0; i < tape_rn[r].n; i++) {
            const tape_note_t *e = &tape_nt[(tape_rn[r].first + i) % TAPE_NOTES];
            if (tape_live(e) && e->len)
                tape_nt[w++] = *e;
        }
        kept = w - first;
        if (kept) {
            tape_rn[nr].first = (uint16_t)first;
            tape_rn[nr].n = (uint16_t)kept;
            tape_rn[nr].cur = 0;
            nr++;
        }
    }
    if (nr > TAPE_RUNS / 2u) {                          /* many runs: one, in the order of time (stable) */
        for (i = 1; i < w; i++) {
            tape_note_t x = tape_nt[i];
            uint32_t j = i;
            while (j > 0u && tape_st_of(&tape_nt[j - 1u]) > tape_st_of(&x)) {
                tape_nt[j] = tape_nt[j - 1u];
                j--;
            }
            tape_nt[j] = x;
        }
        tape_rn[0].first = 0;
        tape_rn[0].n = (uint16_t)w;
        tape_rn[0].cur = 0;
        nr = w ? 1u : 0u;
    }
    if (w != tape.n || nr != tape.nrun)                 /* (notes moved or gone: nothing to take back any more) */
        tape.back = TU_NONE;
    tape.n = (uint16_t)w;
    tape.nrun = (uint8_t)nr;
    tape.rrun = 0;
    if (!w)
        tape.len = 0;
    tape.gen++;
    tape.busy = 0;
}

/* REC on the tape: a take begins (stopped: at the first note) or ends. With LOOP on and no end yet, ending it while
 * the tape rolls is the loop's end: round again from there */
static void tape_rec_toggle(void)
{
    if (tape.rec || tape.q_arm) {
        tape.q_arm = 0;
        tape.q_disarm = 1;
    } else {
        if (tape.back != TU_CLEAR)                       /* (a wipe that can still be undone: its notes stay where
                                                         * they are until the next take is kept) */
            tape_tidy();
        tape.q_disarm = 0;
        tape.q_arm = 1;
    }
}
static int tape_rec_on(void) { return tape.rec || tape.q_arm; }
static void tape_clear(uint32_t mask) { tape.q_clr = (uint8_t)(tape.q_clr | mask); }
static void tape_undo_ask(void) { tape.q_undo = 1; }
static void tape_end_turn(int32_t steps)                /* END's knob: the tape's end, 10 ms a step */
{
    int32_t v = (int32_t)tape.len + steps * 441;
    if (!tape.len)
        return;
    tape.len = (uint32_t)(v < (int32_t)TAPE_MIN_LEN ? (int32_t)TAPE_MIN_LEN : v > (int32_t)TAPE_POS_MAX ?
                          (int32_t)TAPE_POS_MAX : v);
}
/* everything gone, at once (a project loads, power-on): stopped only */
static void tape_reset(void)
{
    tape.busy = 1;
    tape.n = 0;
    tape.nrun = tape.nact = tape.nopen = 0;
    tape.len = tape.pos = 0;
    tape.rec = tape.run = tape.loop = tape.full = tape.back = 0;
    tape.pre = tape.lead = 0;
    tape.ended = 0;
    tape.q_arm = tape.q_disarm = tape.q_undo = tape.q_clr = 0;
    tape.q_retake = 0;
    tape.rrun = 0;
    tape.gen++;
    tape.busy = 0;
}
