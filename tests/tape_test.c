/* SPDX-License-Identifier: GPL-3.0-only */
/* TAPE (src/tape.c): what is played is kept as played. Against the keys' real path (input_on / input_off) and the
 * audio ISR's block loop (events_block), a block of 128 samples at a time:
 *  1. timing: notes played off any grid come back on the same block they were played on, each as long as it was held;
 *     two notes inside one sequencer step keep their own times; a chord of six notes is kept whole;
 *  2. no length set beforehand: armed while stopped, the first note starts the transport at the tape's start; the
 *     tape's end is where the take stopped (a 45 s take: past FREE TAKE's 30 s); the patterns' steps are untouched;
 *  3. layers: a second take on another track while the first plays; both come back; a take that runs on past the
 *     end moves the end;
 *  4. LOOP: round again at the end, every pass the same; what is recorded during a pass plays from the next; a note
 *     held over the end keeps its length; REC again with no end yet is the loop's end;
 *  5. CLEAR a track, UNDO it; UNDO a take; the pool full: no more notes taken, the rest plays on;
 *  6. tape_tidy: the cleared notes out and many runs made one, the same notes at the same times after it.
 * Built and run by work/host_tests.sh (as tests/run_tests.sh builds the others). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define BLK 128u
typedef struct { uint32_t blk; uint8_t trk, note, on; } ev_t;
static ev_t ev[4096];
static uint32_t ev_n, blk_now;
static uint8_t was[NTRK][128];

static void ev_reset(void) { ev_n = 0; blk_now = 0; memset(was, 0, sizeof was); }
/* one audio block; then what each track's voices hold: a note newly held or let go is an event of this block */
static void block(void)
{
    uint32_t k, i, n;
    uint8_t now[128];
    events_block(BLK);
    for (k = 0; k < NTRK; k++) {
        memset(now, 0, sizeof now);
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].gate)
                now[trk[k].v[i].note & 127u] = 1;
        for (n = 0; n < 128u; n++)
            if (now[n] != was[k][n] && ev_n < 4096u) {
                ev[ev_n++] = (ev_t){blk_now, (uint8_t)k, (uint8_t)n, now[n]};
                was[k][n] = now[n];
            }
    }
    blk_now++;
}
static void blocks(uint32_t n) { while (n--) block(); }
static int ev_at(uint32_t b, uint32_t k, uint32_t note, uint32_t on)
{
    uint32_t i;
    for (i = 0; i < ev_n; i++)
        if (ev[i].blk == b && ev[i].trk == k && ev[i].note == note && ev[i].on == on)
            return 1;
    return 0;
}
static uint32_t ev_count(uint32_t k, uint32_t on)
{
    uint32_t i, c = 0;
    for (i = 0; i < ev_n; i++)
        c += (ev[i].trk == k || k >= NTRK) && ev[i].on == on;
    return c;
}
static void fresh(void)                               /* power-on, six plain polyphonic tracks, nothing in the steps */
{
    uint32_t k, i;
    ui_power_on();
    stop_transport();
    tape_reset();
    tape.to_pat = 0;                                  /* (REC TO TAPE, as the unit powers on: main.c tape_power_on) */
    for (k = 0; k < NTRK; k++) {
        track_defaults(&trk[k]);
        trk[k].eng_req = trk[k].engine = 0;           /* ANALOG */
        trk[k].p[P_AMODE] = 0;
        trk[k].p[P_MUTE] = 0;
        for (i = 0; i < NSTEP; i++)
            trk[k].step[i] = (step_t){{0}, 0, ST_REST};
    }
    song.rec = 0;
    ev_reset();
}
static void stop(void) { transport_req = 2; block(); }
static void play(void) { transport_req = 1; }
/* a take on track k: REC, then the notes (block on, blocks held, note), in the order of their starts; `last` blocks in
 * all (0: stop right after the last release) */
typedef struct { uint32_t on, dur; uint8_t note; } pn_t;
static void take(uint32_t k, const pn_t *p, uint32_t n, uint32_t last, int stop_it)
{
    uint32_t b = 0, i, end = 0, started = 0;
    for (i = 0; i < n; i++)
        end = p[i].on + p[i].dur > end ? p[i].on + p[i].dur : end;
    end = last > end ? last : end + 1u;
    sel_set(k);
    if (!tape_rec_on())
        tape_rec_toggle();
    block();                                          /* (the ask reaches the ISR) */
    ev_reset();
    /* the first note starts the transport: its block is block 0 of the tape */
    for (b = 0; b <= end; b++) {
        for (i = 0; i < n; i++) {                     /* (the keys are read at a block's start: before its events) */
            if (p[i].on + p[i].dur == b)
                input_off(&trk[k], p[i].note);
        }
        for (i = 0; i < n; i++)
            if (p[i].on == b) {
                input_on(&trk[k], p[i].note, 100);
                started = 1;
            }
        (void)started;
        if (b < end)
            block();
    }
    if (stop_it)
        stop();
}

static int test_timing(void)
{
    /* off any grid: at 120 BPM a 1/16 step is 5512 samples = 43 blocks; these starts are no multiples of it */
    static const pn_t P[] = {{0, 20, 60}, {7, 9, 64}, {31, 50, 67}, {33, 3, 72}, {100, 61, 48}, {177, 5, 50}};
    int bad = 0;
    uint32_t i, ok = 1, n = sizeof P / sizeof P[0];
    step_t before[NSTEP];
    fresh();
    memcpy(before, trk[0].step, sizeof before);
    take(0, P, n, 0, 1);
    bad += check("armed while stopped: the first note started the transport, at the tape's start",
                 tape.n == n && tape_st_of(&tape_nt[0]) == 0u && !tape.rec && !tape.run);
    for (i = 0; i < n; i++)
        ok &= tape_st_of(&tape_nt[i]) == P[i].on * BLK && tape_nt[i].len == P[i].dur;
    bad += check("every note: its start to the block, its length as held (no step, no grid)", ok);
    bad += check("the tape's end is where the take stopped; no length was set", tape.len == 183u * BLK && tape.len);
    bad += check("the track's steps are untouched (the tape is not the pattern)",
                 !memcmp(before, trk[0].step, sizeof before));
    ev_reset();
    play();
    blocks(200);
    for (i = 0, ok = 1; i < n; i++)                   /* (PLAY's block is the tape's first: block 0) */
        ok &= ev_at(P[i].on, 0, P[i].note, 1) && ev_at(P[i].on + P[i].dur, 0, P[i].note, 0);
    bad += check("played back: every note on the block it was played on, let go after its own length", ok);
    bad += check("two notes 2 blocks (6 ms) apart inside one step: each its own time",
                 ev_at(31, 0, 67, 1) && ev_at(33, 0, 72, 1));
    stop();
    return bad;
}

/* on the unit a key is read after the block's transport requests: the first note of a take armed while stopped
 * sounds a block before the tape starts. What follows must keep its distance from it all the same */
static int test_first_note(void)
{
    int bad = 0;
    uint32_t k, ok = 1;
    for (k = 1; k <= 3u; k++) {                       /* the start k blocks after the key (the unit: 1) */
        uint32_t i;
        fresh();
        sel_set(0);
        tape_rec_toggle();
        block();
        ev_reset();
        input_on(&trk[0], 60, 100);                   /* block 0 renders it; the request waits */
        transport_req = 0;
        for (i = 0; i < k; i++)
            block();                                  /* (k blocks pass, stopped) */
        transport_req = 1;                            /* the start: block k */
        blocks(40u - k);
        input_on(&trk[0], 64, 100);                   /* 40 blocks after the first */
        blocks(8);
        input_off(&trk[0], 60);                       /* held 48 blocks */
        blocks(8);
        input_off(&trk[0], 64);                       /* held 16 */
        blocks(20);
        stop();
        ok &= tape.n == 2u && tape_st_of(&tape_nt[0]) == 0u && tape_st_of(&tape_nt[1]) == 40u * BLK &&
              tape_nt[0].len == 48u && tape_nt[1].len == 16u;
        ev_reset();
        play();
        blocks(80);
        ok &= ev_at(0, 0, 60, 1) && ev_at(40, 0, 64, 1) && ev_at(48, 0, 60, 0) && ev_at(56, 0, 64, 0);
        stop();
    }
    bad += check("a first note that sounds before the tape starts: the next one keeps its distance, both their lengths",
                 ok);
    return bad;
}

static int test_chord(void)
{
    static const pn_t P[] = {{0, 40, 48}, {0, 40, 52}, {0, 40, 55}, {0, 40, 59}, {0, 40, 62}, {0, 40, 66}};
    int bad = 0;
    uint32_t i, ok = 1;
    fresh();
    take(1, P, 6, 0, 1);
    bad += check("a chord of six notes: six on the tape (a step holds four)", tape.n == 6u && tape_count(1) == 6u);
    ev_reset();
    play();
    blocks(60);
    for (i = 0; i < 6u; i++)
        ok &= ev_at(0, 1, P[i].note, 1) && ev_at(40, 1, P[i].note, 0);
    bad += check("played back: the six together, and let go together", ok && ev_count(1, 1) == 6u);
    stop();
    return bad;
}

static int test_long(void)
{
    /* 45 s: a note every second, the last at 44 s (FREE TAKE keeps 30 s; a pattern 64 steps) */
    static pn_t P[45];
    int bad = 0;
    uint32_t i, ok = 1, sec = 44100u / BLK;           /* 344 blocks */
    for (i = 0; i < 45u; i++)
        P[i] = (pn_t){i * sec + (i ? 3u : 0u), 100, (uint8_t)(40 + i % 24)};
    fresh();
    take(2, P, 45, 0, 1);
    bad += check("a 45 s take, never told how long: 45 notes, the end past 44 s",
                 tape.n == 45u && tape.len > 44u * 44100u && tape.len < 46u * 44100u);
    ev_reset();
    play();
    blocks(45u * sec + 200u);
    for (i = 1; i < 45u; i++)
        ok &= ev_at(P[i].on, 2, P[i].note, 1);
    bad += check("played back: the 45 notes, each on its block", ok && ev_count(2, 1) == 45u);
    stop();
    return bad;
}

static int test_layers(void)
{
    static const pn_t A[] = {{0, 30, 60}, {50, 30, 62}, {100, 30, 64}};
    static const pn_t B[] = {{0, 10, 36}, {26, 10, 38}, {75, 10, 36}, {130, 40, 41}};
    int bad = 0;
    uint32_t len1;
    fresh();
    take(0, A, 3, 140, 1);                            /* the first layer: 140 blocks */
    len1 = tape.len;
    bad += check("the first take: three notes, its end at the stop", tape.n == 3u && len1 == 140u * BLK);
    take(3, B, 4, 0, 0);                              /* the second, on track 4, the first playing meanwhile */
    bad += check("the second take hears the first: its notes on their blocks",
                 ev_at(0, 0, 60, 1) && ev_at(50, 0, 62, 1) && ev_at(100, 0, 64, 1) && ev_at(130, 0, 64, 0));
    stop();
    bad += check("two layers: 3 notes on track 1, 4 on track 4", tape_count(0) == 3u && tape_count(3) == 4u);
    bad += check("the second take ran on past the end: the end moved to its stop",
                 tape.len > len1 && tape.len == 171u * BLK);
    ev_reset();
    play();
    blocks(190);
    bad += check("played back: both layers, each note on its block",
                 ev_at(0, 0, 60, 1) && ev_at(50, 0, 62, 1) && ev_at(100, 0, 64, 1) && ev_at(0, 3, 36, 1) &&
                 ev_at(26, 3, 38, 1) && ev_at(75, 3, 36, 1) && ev_at(130, 3, 41, 1) && ev_at(170, 3, 41, 0) &&
                 ev_count(0, 1) == 3u && ev_count(3, 1) == 4u);
    stop();
    return bad;
}

static int test_loop(void)
{
    static const pn_t A[] = {{0, 12, 60}, {40, 12, 64}, {80, 12, 67}};
    int bad = 0;
    uint32_t L = 120u, ok = 1, p, b0;
    fresh();
    take(0, A, 3, L, 1);
    bad += check("the take: 120 blocks", tape.len == L * BLK);
    tape.loop = 1;
    ev_reset();
    play();
    blocks(1);                                        /* PLAY's block: the tape at 0 after it */
    b0 = blk_now;
    blocks(4u * L);
    for (p = 0; p < 4u; p++)                          /* every pass the same: the notes L blocks apart */
        ok &= ev_at(b0 + p * L + 39u, 0, 64, 1) && ev_at(b0 + p * L + 79u, 0, 67, 1) &&
              ev_at(b0 + p * L + 51u, 0, 64, 0);
    bad += check("LOOP: round again at the end, four passes alike (and the fifth's first note)",
                 ok && ev_count(0, 1) == 13u && ev_at(4u * L, 0, 60, 1));
    /* overdub on track 2 during the pass that starts now: notes at +10 and +100 (held 40: over the end) */
    sel_set(1);
    tape_rec_toggle();
    ev_reset();
    b0 = 0;
    {
        uint32_t b;
        for (b = 0; b < 3u * L; b++) {
            if (b == 10u) input_on(&trk[1], 72, 100);
            if (b == 22u) input_off(&trk[1], 72);
            if (b == 100u) input_on(&trk[1], 76, 100);
            if (b == 140u) input_off(&trk[1], 76);    /* (20 blocks into the next pass) */
            if (b == 150u) tape_rec_toggle();         /* REC again: the take ends, the tape rolls on */
            block();
        }
    }
    bad += check("recorded in a pass: two notes on track 2, the tape's end as it was",
                 tape_count(1) == 2u && tape.len == L * BLK && !tape.rec && tape.run);
    bad += check("the note held over the end: its whole length (40 blocks)",
                 tape_nt[4].len == 40u && tape_st_of(&tape_nt[4]) == 101u * BLK);
    /* (as played: 72 at b 10..22, 76 at 100..140; the next passes: 72 at L+10, 76 at L+100, from the tape) */
    bad += check("what a pass recorded plays from the next pass on, at the same place",
                 ev_at(L + 10u, 1, 72, 1) && ev_at(L + 22u, 1, 72, 0) && ev_at(2u * L + 10u, 1, 72, 1) &&
                 ev_at(L + 100u, 1, 76, 1) && ev_at(L + 140u, 1, 76, 0) && ev_at(2u * L + 100u, 1, 76, 1));
    stop();
    /* REC again with LOOP on and no end yet: that moment is the loop's end */
    fresh();
    tape.loop = 1;
    sel_set(0);
    tape_rec_toggle();
    block();
    ev_reset();
    input_on(&trk[0], 60, 100);
    blocks(10);
    input_off(&trk[0], 60);
    blocks(70);                                       /* (the transport started on block 1: the tape is at 79 blocks) */
    tape_rec_toggle();
    block();
    bad += check("LOOP, no end yet, REC again while it rolls: the end is there, and it goes round",
                 tape.len == 80u * BLK && tape.run && !tape.rec && tape.pos < tape.len);
    ev_reset();
    blocks(200);
    bad += check("... the phrase again every 80 blocks, as long as it was held (10)", ev_n >= 6u && ev[2].on &&
                 ev[4].on && ev[4].blk - ev[2].blk == 80u && !ev[3].on && ev[3].blk - ev[2].blk == 10u);
    stop();
    return bad;
}

static int test_clear_undo(void)
{
    static const pn_t A[] = {{0, 12, 60}, {40, 12, 64}};
    static const pn_t B[] = {{5, 12, 36}, {45, 12, 38}};
    int bad = 0;
    fresh();
    take(0, A, 2, 100, 1);
    take(1, B, 2, 90, 1);
    bad += check("two takes: 2 + 2 notes", tape_count(0) == 2u && tape_count(1) == 2u && tape.back == TU_TAKE);
    tape_undo_ask();
    block();
    bad += check("UNDO: the last take gone, the first as it was", tape_count(0) == 2u && tape_count(1) == 0u &&
                 tape.n == 2u && tape.len == 100u * BLK);
    take(1, B, 2, 90, 1);
    tape_clear(1u << 0);
    block();
    bad += check("CLEAR track 1: its notes gone, track 2's stay", tape_count(0) == 0u && tape_count(1) == 2u);
    ev_reset();
    play();
    blocks(110);
    bad += check("... and only track 2 plays", ev_count(0, 1) == 0u && ev_count(1, 1) == 2u);
    stop();
    tape_undo_ask();
    block();
    bad += check("UNDO: track 1's notes back", tape_count(0) == 2u && tape_count(1) == 2u);
    tape_clear(3u);
    block();
    bad += check("CLEAR both: an empty tape has no end", tape_count(NTRK) == 0u && tape.len == 0u);
    tape_undo_ask();
    block();
    bad += check("UNDO: both back, and the end", tape_count(NTRK) == 4u && tape.len == 100u * BLK);
    /* cleared, then a new take: the cleared notes are gone for good (a wipe stays undoable until a take is kept after
     * it: REC held, ui_tape.c), tidied away at the arming after; the rest as before */
    tape_clear(1u << 0);
    block();
    take(2, A, 2, 60, 1);
    bad += check("a take after a clear: the cleared notes stay gone, the undo is the take's now",
                 tape_count(1) == 2u && tape_count(2) == 2u && tape_count(0) == 0u && tape.back == TU_TAKE);
    tape_rec_toggle(); block();
    tape_rec_toggle(); block();
    bad += check("... and the next arming tidies the pool: 2 + 2 notes, none cleared left", tape.n == 4u);
    ev_reset();
    play();
    blocks(110);
    bad += check("... played back: track 2's and track 3's, on their blocks",
                 ev_at(0, 1, 36, 1) && ev_at(40, 1, 38, 1) && ev_at(0, 2, 60, 1) && ev_at(40, 2, 64, 1) &&
                 ev_count(NTRK, 1) == 4u);
    stop();
    return bad;
}

static int test_full(void)
{
    int bad = 0;
    uint32_t i, b;
    fresh();
    sel_set(0);
    tape_rec_toggle();
    block();
    for (i = 0, b = 0; i < TAPE_NOTES + 40u; i++) {    /* more notes than the pool holds, 2 blocks each */
        input_on(&trk[0], 40u + i % 40u, 100);
        block();
        input_off(&trk[0], 40u + i % 40u);
        block();
        b += 2u;
    }
    bad += check("the pool full: it holds what fits, says so, and writes nowhere else",
                 tape.n == TAPE_NOTES && tape.full == TF_NOTES && tape.nrun == 1u);
    stop();
    bad += check("... the take ends as any other", !tape.rec && tape.len && tape_count(0) == TAPE_NOTES);
    return bad;
}

static int test_tidy(void)
{
    int bad = 0;
    uint32_t p, i, ok = 1, L = 60u;
    static const pn_t A[] = {{0, 5, 60}};
    fresh();
    take(0, A, 1, L, 1);
    tape.loop = 1;
    play();
    blocks(1);
    sel_set(1);
    tape_rec_toggle();
    for (p = 0; p < 20u; p++)                         /* twenty passes, a note in each: twenty runs */
        for (i = 0; i < L; i++) {
            if (i == 10u + p) input_on(&trk[1], 70u + p, 100);
            if (i == 14u + p) input_off(&trk[1], 70u + p);
            block();
        }
    stop();
    bad += check("twenty passes with a note each: twenty-one runs", tape.nrun == 21u && tape.n == 21u);
    tape_tidy();
    bad += check("tidied: one run, the notes in the order of time", tape.nrun == 1u && tape.n == 21u);
    for (i = 1; i < tape.n; i++)
        ok &= tape_st_of(&tape_nt[i - 1u]) <= tape_st_of(&tape_nt[i]);
    bad += check("... sorted", ok);
    tape.loop = 0;
    ev_reset();
    play();
    blocks(L + 5u);
    for (p = 0, ok = 1; p < 20u; p++)
        ok &= ev_at(11u + p, 1, 70u + p, 1);
    bad += check("... and they play where they were played", ok && ev_at(0, 0, 60, 1) && ev_count(NTRK, 1) == 21u);
    stop();
    return bad;
}

/* through the panel: SEQ > TAPE, its REC, the keys, PLAY, its knobs, SHIFT + EDIT, SHIFT + OCT-; REC elsewhere */
static int test_tape_panel(void)
{
    int bad = 0;
    uint32_t len0;
    fresh();
    tape.to_pat = 1;                                  /* (Skiff's layout: the tape has a page of its own) */
    go_page(GR_TAPE); frame();
    bad += check("SEQ > TAPE: the page, the last of SEQ's", str_eq(cur_page()->title, "TAPE") && cur_page()->fam == FAM_SEQ);
    press(B_REC);
    bad += check("REC there: the tape armed (not the steps), nothing starts yet, the page says how",
                 tape_rec_on() && !song.rec && !transport_req && msg_is("PLAY A KEY TO START"));
    block();
    fm1_in.notes = host_notes = 1u << 7;              /* a key down */
    block();
    bad += check("the first key: its note on the tape, the transport asked to start", tape.n == 1u && transport_req == 1u);
    blocks(50);
    fm1_in.notes = host_notes = 0;                    /* and up, 50 blocks later */
    blocks(50);
    bad += check("... it rolls, recording", song.playing && tape.run && tape.rec);
    press(B_PLAY);
    block();
    bad += check("PLAY: stopped, the take kept: one note as long as the key was down, the end where it stopped",
                 !song.playing && !tape.rec && tape_count(0) == 1u && tape_nt[0].len >= 50u && tape_nt[0].len <= 51u &&
                 tape.len >= 99u * BLK && tape.len <= 101u * BLK);
    frame();
    bad += check("the tape's own REC mark is off again, the steps were never armed", !tape_rec_on() && !song.rec);
    turn(EN_K1, 1);
    bad += check("KNOB 1: LOOP ON", tape.loop == 1u);
    turn(EN_K1, -1);
    bad += check("... and OFF", tape.loop == 0u);
    len0 = tape.len;
    turn(EN_K2, -3);
    bad += check("KNOB 2: END earlier, 10 ms a click", tape.len == len0 - 3u * 441u);
    turn(EN_K2, 3);
    bad += check("... and back", tape.len == len0);
    press(B_OCTUP);
    bad += check("OCT+ there: the octave, as on any page that is no action page", song.octave == 1);
    press(B_OCTDN);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM there: the track (the layer to record next)", song.sel == 1u);
    turn(EN_ALGO, -1);
    shift_lock(B_EDIT);
    block();
    bad += check("SHIFT + EDIT: the track's notes off the tape; no EDIT layer locked, the page stays",
                 tape_count(0) == 0u && ui.lock != LAYER_EDIT && str_eq(cur_page()->title, "TAPE") && !ui.home);
    shift_lock(B_OCTDN);
    block();
    bad += check("SHIFT + OCT-: taken back; the octave as it was", tape_count(0) == 1u && tape.len == len0 &&
                 song.octave == 0);
    shift_lock(B_OCTDN);
    bad += check("... again: nothing to take back, said so", msg_is("NOTHING TO TAKE BACK"));
    frames(1600);
    press(B_REC);                                     /* a take, then REC pressed on another page: it ends the take */
    block();
    go_title("ENV"); frame();
    bad += check("recording, on another page", tape_rec_on() && str_eq(cur_page()->title, "ENV"));
    frames(400);
    press(B_REC);
    block();
    bad += check("REC there while the tape records: the take ends, the steps are not armed", !tape_rec_on() && !song.rec);
    frames(400);
    stop_transport();
    return bad;
}

/* the new layout, as the unit powers on (main.c tape_power_on): HOME is the tape's screen and REC records onto it;
 * LOOP off it plays once and stops; a later take may run past the end; PLAY held: LOOP; REC held: another take (the
 * track wiped, a take armed), SAVE held undoes it; SEQ is the step sequencer (its REC the steps', REC held clears
 * them), SAVE's PATTERNS are there */
static void hold_btn(uint32_t label, int blocks_too)  /* held past MENU > HOLD's time, the ISR running, then let go */
{
    uint32_t k;
    btn_down(label);
    for (k = 0; k < 52u; k++) {
        frame();
        if (blocks_too)
            block();
    }
    btn_up(label);
    frame();
    block();
    frame();
    block();
}
static int test_tape_first(void)
{
    int bad = 0;
    uint32_t i, any = 0, len1, seen = 0;
    fresh();
    tape_power_on();
    go_home(); frame();
    bad += check("power-on: the new layout, HOME", UI_NEW && ui.home);
    press(B_REC);
    bad += check("REC on HOME: the tape armed, not the steps; nothing starts yet", tape_rec_on() && !song.rec && !transport_req &&
                 !song.playing && ui.home);
    block();
    fm1_in.notes = host_notes = 1u << 7;              /* a key down, 40 blocks, up, 60 more */
    block();
    blocks(40);
    fm1_in.notes = host_notes = 0;
    blocks(60);
    bad += check("the first key starts it: rolling, recording, a note on the tape", song.playing && tape.rec && tape.n == 1u);
    press(B_REC);
    block();
    bad += check("REC again: the take ends there", !tape_rec_on() && tape.len >= 99u * BLK);
    len1 = tape.len;
    for (i = 0; i < NSTEP; i++)
        any |= trk[0].step[i].n | trk[0].step[i].hit;
    bad += check("... nothing went into the steps", !any && !song.rec);
    ev_reset();
    blocks(10);
    bad += check("LOOP off: at its end it stops by itself and is rewound, the note not played again", !song.playing &&
                 !tape.run && !tape.pos && ev_count(NTRK, 1) == 0u);
    press(B_PLAY);
    block();
    ev_reset();
    blocks(len1 / BLK + 10u);
    bad += check("PLAY: the tape once, from its start, then stopped", !song.playing && ev_count(0, 1) == 1u);
    turn(EN_ALGO, 1);                                 /* the next layer, longer than the first */
    press(B_REC);
    block();
    fm1_in.notes = host_notes = 1u << 9;
    block();
    blocks(len1 / BLK + 60u);
    bad += check("track 2 armed and played from stopped: rolling past the first take's end, still recording",
                 song.sel == 1u && song.playing && tape.rec && tape.pos > len1);
    fm1_in.notes = host_notes = 0;
    blocks(20);
    press(B_PLAY);
    block();
    bad += check("PLAY: stopped; the end moved out to where this take stopped; a note on each track",
                 !song.playing && tape.len > len1 + 70u * BLK && tape_count(0) == 1u && tape_count(1) == 1u);
    len1 = tape.len;
    hold_btn(B_PLAY, 1);
    bad += check("PLAY held: it plays (its press) and LOOP is on, said so", tape.loop == 1u && song.playing);
    ev_reset();
    blocks(2u * (len1 / BLK) + 20u);
    bad += check("... it goes round, both notes played again each time", song.playing && ev_count(0, 1) >= 2u &&
                 ev_count(1, 1) >= 2u);
    shift_lock(B_PLAY);
    block();
    bad += check("SHIFT + PLAY: LOOP off again, still playing", tape.loop == 0u && song.playing);
    press(B_PLAY);
    block();
    frame();
    bad += check("PLAY: stopped", !song.playing && !tape.run);
    hold_btn(B_REC, 1);                               /* another take on track 2: wiped, armed, stopped */
    bad += check("REC held: track 2's tape wiped, track 1's kept, a take armed, nothing rolling",
                 tape_count(1) == 0u && tape_count(0) == 1u && tape_rec_on() && !song.playing && song.sel == 1u);
    fm1_in.notes = host_notes = 1u << 5;              /* the other shot at it */
    block();
    blocks(30);
    fm1_in.notes = host_notes = 0;
    blocks(30);
    press(B_REC);
    block();
    blocks(4);
    frame();
    bad += check("... and played again: one new note on track 2", tape_count(1) == 1u && !tape_rec_on());
    hold_btn(B_REC, 1);
    bad += check("REC held once more: wiped again", tape_count(1) == 0u && tape_rec_on());
    press(B_REC);                                     /* (not wanted after all: REC, the take's off) */
    block();
    frame();
    hold_btn(B_SAVE, 1);
    bad += check("SAVE held: the wipe undone, track 2's note is back", tape_count(1) == 1u && tape_count(0) == 1u);
    for (i = 0; i < 8u; i++) {                        /* SEQ: the step sequencer's pages, PATTERNS among them, no TAPE */
        press(B_SEQ); frame();
        seen |= (uint32_t)(cur_page()->graph == GR_BANK) | (uint32_t)(cur_page()->graph == GR_TAPE) << 1 |
                (uint32_t)(page_fam(cur_page()) != FAM_SEQ) << 2;
    }
    bad += check("SEQ: its pages only, SAVE's PATTERNS among them, no TAPE page", seen == 1u && !ui.home);
    go_page(GR_ROLL); frame();
    press(B_REC);
    bad += check("REC on a SEQ page: the steps' recording, Skiff's own; the tape is not armed", song.rec == 2u && !tape_rec_on());
    press(B_REC);
    stop_transport();
    trk[1].step[3] = (step_t){{60}, 1, ST_NOTE};
    frame();
    hold_btn(B_REC, 0);
    bad += check("REC held there: the selected track's steps cleared (one undo), its tape untouched",
                 seq_is_empty(&trk[1]) && tape_count(1) == 1u);
    stop_transport();
    go_home(); frame();
    return bad;
}

#ifndef TAPE_TEST_NO_MAIN
int main(void)
{
    int bad = 0;
    bad += test_timing();
    bad += test_first_note();
    bad += test_chord();
    bad += test_long();
    bad += test_layers();
    bad += test_loop();
    bad += test_clear_undo();
    bad += test_full();
    bad += test_tidy();
    bad += test_tape_panel();
    bad += test_tape_first();
    printf(bad ? "tape: %d FAILED\n" : "tape: all ok\n", bad);
    return bad != 0;
}
#endif
