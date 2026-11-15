/* SPDX-License-Identifier: GPL-3.0-only */
/* The tape kept in flash (firmware/src/tape_store.c) against simulated NOR flash, with storage.c, the projects and the
 * autosave as the firmware has them:
 *  1. a tape saved and loaded back whole (one chunk, two chunks, the pool full): the live, finished notes in the order
 *     of time, its end, LOOP; a wipe that can still be undone stays so after a save;
 *  2. where: only the third user-sample slot's sectors are written, apart from every storage.c object, the pattern
 *     pool and the two user-sample slots left; a chunk an erase;
 *  3. a write torn at any page, or an erase that fails, leaves the tape saved before whole and in charge (every pair
 *     of sizes, torn again and again);
 *  4. never while the transport runs or a take is armed;
 *  5. with the projects: SAVE keeps the tape, LOAD brings it back (another slot's another tape), a project with no
 *     tape gives an empty one, DELETE drops it, NEW empties it; a changed tape is a changed project;
 *  6. the autosave: the tape as it was comes back at power-on; an unchanged tape is not written again.
 * Built and run by work/host_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int fail_after = -1, erase_fail;
static uint32_t erases[256], progs;               /* per sector (offset >> 12); programs asked for */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases[(off >> 12) & 255u]++;
    if (erase_fail)
        return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* (fail_after: the power goes after that many) */
{
    const uint8_t *s = src;
    uint32_t i;
    progs++;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/bank.c"

static int check(const char *what, int ok)
{
    printf("tape_store: %-90s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
static void fresh(void)                           /* a new unit: flash erased, the tracks at power-on, an empty tape */
{
    memset(nor, 0xFF, sizeof nor);
    memset(erases, 0, sizeof erases);
    memset(&song, 0, sizeof song);
    sel_set(0);
    memset(trk, 0, sizeof trk);
    memset(&ui, 0, sizeof ui);
    memset(&motion, 0, sizeof motion);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(&as, 0, sizeof as);
    host_tracks_init();
    undo_clear();
    flash_ok = 1;
    fail_after = -1;
    erase_fail = 0;
    bank_boot();
    lists_clear();
    motion_lost = 0;
    transport_req = 0;
    tape_reset();
    tape.to_pat = 0;
    proj_cur = PROJ_NO_SLOT;
    proj_name[0] = 0;
    project_clean();
}
static uint32_t erases_in(uint32_t lo, uint32_t hi)   /* erases of the sectors lo .. hi - 1 (flash offsets) */
{
    uint32_t i, n = 0;
    for (i = lo >> 12; i < hi >> 12; i++)
        n += erases[i];
    return n;
}
static uint32_t erases_all(void) { return erases_in(0, 0x100000u); }

/* a tape of n notes in `runs` runs (each in the order of time, the runs interleaved), every 7th cleared when `holes`,
 * one still held; seed: another tape. What a save keeps of it -> want[], in the order of time: its count */
static tape_note_t want[TAPE_NOTES];
static uint32_t make(uint32_t n, uint32_t runs, int holes, uint32_t seed)
{
    uint32_t i, r, w = 0, per = runs ? (n + runs - 1u) / runs : 0u, sd = seed * 7919u + 1u, nw = 0, j;
    tape_reset();
    for (r = 0; r < runs && w < n; r++) {
        uint32_t cnt = n - w < per ? n - w : per;
        tape_rn[r].first = (uint16_t)w;
        tape_rn[r].n = (uint16_t)cnt;
        tape_rn[r].cur = 0;
        for (i = 0; i < cnt; i++, w++) {
            tape_note_t *e = &tape_nt[w];
            uint32_t k = (i + r + seed) % NTRK;
            sd = sd * 1664525u + 1013904223u;
            e->st = (uint32_t)((uint64_t)i * (60u * 44100u) / (cnt + 1u)) + r * 37u + (sd >> 24);   /* (rising in a run) */
            e->st |= k << 28;
            e->len = (uint16_t)(1u + (sd >> 12) % 2000u);
            e->note = (uint8_t)(24u + (sd >> 4) % 80u);
            e->vel = (uint8_t)(1u + (sd >> 20) % 127u);
            if (holes && w % 7u == 3u)
                e->st |= TAPE_DEL;
            if (holes && w == n / 2u)
                e->len = 0;                          /* (still held: not kept) */
        }
        tape.nrun = (uint8_t)(r + 1u);
    }
    tape.n = (uint16_t)w;
    tape.len = 61u * 44100u + seed;
    tape.loop = (uint8_t)(seed & 1u);
    tape.gen++;
    for (i = 0; i < w; i++)                          /* what is kept, sorted by time (stable) */
        if (tape_live(&tape_nt[i]) && tape_nt[i].len) {
            tape_note_t x = tape_nt[i];
            for (j = nw; j > 0u && tape_st_of(&want[j - 1u]) > tape_st_of(&x); j--)
                want[j] = want[j - 1u];
            want[j] = x;
            nw++;
        }
    return nw;
}
/* the tape is what make() said a save keeps: nw notes, the same ones, in the order of time, one run, the end, LOOP */
static int same(uint32_t nw, uint32_t seed)
{
    uint32_t i, ok = tape.n == nw && tape.nrun == (nw ? 1u : 0u) && (!nw || (tape_rn[0].first == 0 && tape_rn[0].n == nw));
    uint64_t a = 0, b = 0;
    ok = ok && tape.len == (nw ? 61u * 44100u + seed : 0u) && tape.loop == (nw ? (seed & 1u) : 0u);
    for (i = 0; ok && i < nw; i++) {
        ok = ok && tape_live(&tape_nt[i]) && tape_nt[i].len && (!i || tape_st_of(&tape_nt[i - 1u]) <= tape_st_of(&tape_nt[i]));
        a += (uint64_t)tape_nt[i].st * 31u + tape_nt[i].len * 7u + tape_nt[i].note * 3u + tape_nt[i].vel;
        b += (uint64_t)want[i].st * 31u + want[i].len * 7u + want[i].note * 3u + want[i].vel;
        ok = ok && tape_st_of(&tape_nt[i]) == tape_st_of(&want[i]);
    }
    return ok && a == b;
}

static int test_roundtrip(void)
{
    static const uint32_t N[] = {1u, 120u, 478u, 479u, 700u, TAPE_NOTES};
    int bad = 0;
    uint32_t i, ok1 = 1, ok2 = 1, ok3 = 1, ok4 = 1;
    fresh();
    bad += check("nothing kept: a load gives an empty tape and says so", tape_store_load(0) == 1 && !tape.n && !tape.len);
    for (i = 0; i < sizeof N / sizeof N[0]; i++) {
        uint32_t nw, e0, t0, sig, parts, slot = i % TS_SLOTS;
        nw = make(N[i], 1u + i * 3u, i && i != 5u, 10u + i);
        parts = nw > TS_PER ? 2u : 1u;
        sig = tape_store_sig();
        e0 = erases_all();
        t0 = erases_in(0xC8000u, 0xDC000u);
        ok1 &= tape_store_save(slot) == 0;
        ok2 &= erases_all() - e0 == parts && erases_in(0xC8000u, 0xDC000u) - t0 == erases_all() - e0;
        ok4 &= tape.n == N[i] && tape_store_sig() == sig;              /* (the tape itself untouched) */
        tape_reset();
        ok3 &= tape_store_load(slot) == 0 && same(nw, 10u + i) && tape_store_sig() == sig;
        if (!(ok1 && ok2 && ok3 && ok4))
            printf("  at n=%u nw=%u: save %u where %u load %u untouched %u (n %u)\n", N[i], nw, ok1, ok2, ok3, ok4, tape.n);
    }
    bad += check("1, 120, 478, 479, 700 and 950 notes in 1 to 16 runs: every save succeeds", ok1);
    bad += check("an erase a chunk (478 notes each), all of them in the third user-sample slot's sectors", ok2);
    bad += check("saving leaves the tape as it is (its cleared notes too: still to undo)", ok4);
    bad += check("loaded back: the live, finished notes in the order of time, one run, the end, LOOP, the same signature", ok3);
    {   /* five slots apart */
        uint32_t s, nw[TS_SLOTS], ok = 1;
        fresh();
        for (s = 0; s < TS_SLOTS; s++) {
            nw[s] = make(100u + s * 190u, 2u + s, 1, 40u + s);
            ok &= tape_store_save(s) == 0;
        }
        for (s = 0; s < TS_SLOTS; s++) {
            (void)make(100u + s * 190u, 2u + s, 1, 40u + s);          /* (want[] again) */
            ok &= tape_store_load(s) == 0 && same(nw[s], 40u + s);
        }
        bad += check("the four projects' tapes and the autosave's: each its own", ok);
    }
    {   /* apart from everything else in flash */
        uint32_t o, c, ok = 1;
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++)
                ok &= st_sector(o, c) + ST_SECTOR <= TS_BASE || st_sector(o, c) >= TS_BASE + TS_SLOTS * 4u * ST_SECTOR;
        ok &= SMP_USER_BASE + SMP_USER_FLASH_SLOTS * SMP_USER_SIZE <= TS_BASE && TS_BASE + TS_SLOTS * 4u * ST_SECTOR <= 0xDC000u;
        ok &= !SMP_USER_HAS(2) && SMP_USER_HAS(1) && SMP_USER_HAS(0);
        bad += check("its sectors: after the two user-sample slots, before the user presets, no storage.c object's", ok);
    }
    return bad;
}

/* a save of `b` notes over a save of `a` notes, the power going after every count of programs in turn: what loads is
 * the tape before, whole, until the save is complete */
static int torn(uint32_t a, uint32_t b, uint32_t again)
{
    uint32_t na, k, ok = 1, total, r;
    fresh();
    na = make(a, 3, 1, 5u);
    ok &= tape_store_save(1) == 0;
    (void)make(b, 4, 1, 6u);
    progs = 0;
    {   /* how many programs a whole save takes (on a copy of the flash) */
        static uint8_t keep[0x100000];
        memcpy(keep, nor, sizeof nor);
        ok &= tape_store_save(1) == 0;
        total = progs;
        memcpy(nor, keep, sizeof nor);
    }
    for (k = 0; k < total && ok; k++) {
        for (r = 0; r < again && ok; r++) {              /* (torn at the same place again and again: still the one before) */
            (void)make(b, 4, 1, 6u);
            fail_after = (int)k;
            ok &= tape_store_save(1) != 0;
            fail_after = -1;
            (void)make(a, 3, 1, 5u);                     /* (want[]: the tape before) */
            tape_reset();
            ok &= tape_store_load(1) == 0 && same(na, 5u);
            if (!ok)
                printf("  torn %u over %u at program %u of %u (round %u): n %u\n", b, a, k, total, r, tape.n);
        }
    }
    {   /* and the erase failing */
        (void)make(b, 4, 1, 6u);
        erase_fail = 1;
        ok &= tape_store_save(1) != 0;
        erase_fail = 0;
        (void)make(a, 3, 1, 5u);
        tape_reset();
        ok &= tape_store_load(1) == 0 && same(na, 5u);
    }
    {   /* then a whole save: the new one */
        uint32_t nb = make(b, 4, 1, 6u);
        ok &= tape_store_save(1) == 0;
        tape_reset();
        ok &= tape_store_load(1) == 0 && same(nb, 6u);
    }
    return (int)ok;
}
static int test_torn(void)
{
    int bad = 0;
    bad += check("torn at any page, one chunk over one chunk: the tape before, whole", torn(200, 300, 1));
    bad += check("torn at any page, two chunks over one: the tape before, whole", torn(200, 900, 1));
    bad += check("torn at any page, one chunk over two: the tape before, whole", torn(900, 200, 1));
    bad += check("torn at any page, two chunks over two: the tape before, whole", torn(950, 800, 1));
    bad += check("torn at the same page three times running, two over two: still the tape before", torn(800, 950, 3));
    {   /* a run of saves of every size after one another, each loads back */
        static const uint32_t N[] = {900, 100, 950, 950, 30, 600, 479, 478, 800};
        uint32_t i, ok = 1;
        fresh();
        for (i = 0; i < sizeof N / sizeof N[0]; i++) {
            uint32_t nw = make(N[i], 5, 1, 70u + i);
            ok &= tape_store_save(2) == 0;
            tape_reset();
            ok &= tape_store_load(2) == 0 && same(nw, 70u + i);
        }
        bad += check("nine saves of every size after one another: each loads back whole", ok);
    }
    return bad;
}

static int test_quiet(void)
{
    int bad = 0;
    uint32_t e0, p0, ok;
    fresh();
    (void)make(300, 3, 1, 9u);
    e0 = erases_all();
    p0 = progs;
    song.playing = 1;
    ok = tape_store_save(0) != 0;
    song.playing = 0;
    tape.rec = 1;                                        /* (armed: a key would start a take) */
    ok &= tape_store_save(0) != 0;
    tape.rec = 0;
    tape.q_clr = 1;
    ok &= tape_store_save(0) != 0;
    tape.q_clr = 0;
    bad += check("playing, armed, or a wipe asked for: not saved, the flash untouched", ok && erases_all() == e0 && progs == p0);
    flash_ok = 0;
    ok = tape_store_save(0) != 0 && tape_store_load(0) == 1 && erases_all() == e0;
    flash_ok = 1;
    bad += check("no flash (another part): nothing written, nothing loaded", ok);
    return bad;
}

static int test_project(void)
{
    int bad = 0;
    uint32_t na, nb, ok, e0;
    fresh();
    na = make(700, 4, 1, 21u);
    ok = project_save_as(1, "ONE") == 0 && !project_changed();
    bad += check("SAVE a project: its tape kept with it, the project unchanged after it", ok && erases_in(0xC8000u, 0xDC000u) == 2u);
    nb = make(90, 2, 1, 22u);
    ok = project_changed();
    bad += check("another tape: the project counts as changed", ok);
    ok = project_save_as(2, "TWO") == 0;
    (void)make(5, 1, 0, 23u);
    project_load(1);
    (void)make(700, 4, 1, 21u);                          /* (want[]) */
    tape_reset();
    project_load(1);
    ok &= same(na, 21u) && !project_changed() && proj_cur == 1u;
    bad += check("LOAD it: its tape back, the project unchanged", ok);
    (void)make(90, 2, 1, 22u);
    tape_reset();
    project_load(2);
    bad += check("LOAD the other slot: the other tape", same(nb, 22u) && proj_cur == 2u);
    {   /* a project kept with no tape (an empty one) */
        tape_reset();
        ok = project_save_as(3, "BARE") == 0;
        (void)make(40, 1, 0, 24u);
        project_load(3);
        bad += check("a project saved with an empty tape: LOAD empties the tape", ok && !tape.n && !tape.len);
    }
    project_load(1);
    e0 = erases_in(0xC8000u, 0xDC000u);
    ok = project_erase(1) == 0;
    ok &= erases_in(0xC8000u, 0xDC000u) == e0 + 1u;
    tape_reset();
    ok &= tape_store_load(1) == 1 && !tape.n;
    bad += check("DELETE the project: its tape dropped (an erase)", ok);
    e0 = erases_in(0xC8000u, 0xDC000u);
    ok = project_erase(3) == 0 && erases_in(0xC8000u, 0xDC000u) == e0;
    bad += check("DELETE a project with no tape: no erase for it", ok);
    (void)make(40, 1, 0, 25u);
    project_new();
    bad += check("NEW: an empty tape", !tape.n && !tape.len && !tape.loop);
    {   /* a tape save that fails: said, the project itself kept */
        fresh();
        (void)make(300, 2, 0, 26u);
        tape.rec = 1;                                    /* (armed) */
        ui.msg[0] = 0;
        ok = project_save_as(0, "X") == 0 && !strcmp(ui.msg, "SAVED, NOT THE TAPE") && project_used(0);
        tape.rec = 0;
        bad += check("the tape could not be kept (a take armed): SAVED, NOT THE TAPE", ok);
        ui.msg[0] = 0;
        ok = project_save_as(0, "X") == 0 && !strcmp(ui.msg, "FULL PROJECT SAVED");
        bad += check("kept with its tape: FULL PROJECT SAVED", ok);
    }
    return bad;
}

static void as_run(uint32_t ms)                          /* the main loop for ms (autosave_poll checks every 250 ms) */
{
    uint32_t end = fm1_ms + ms;
    while ((int32_t)(end - fm1_ms) > 0) {
        fm1_ms += 50u;
        autosave_poll();
    }
}
static int test_autosave(void)
{
    int bad = 0;
    uint32_t nw, e0, ok, w0;
    fresh();
    (void)autosave_boot(1);
    as_run(60000u);
    e0 = erases_in(0xC8000u, 0xDC000u);
    w0 = as.writes;
    bad += check("power-on, a minute idle, the tape empty: no tape written", !e0);
    nw = make(600, 3, 1, 31u);
    as_run(30000u);
    ok = as.writes == w0 + 1u && erases_in(0xC8000u, 0xDC000u) == e0 + 2u;
    bad += check("a tape recorded, idle: the autosave writes it once (two chunks: two erases)", ok);
    as_run(600000u);
    bad += check("ten minutes unchanged after it: not written again", erases_in(0xC8000u, 0xDC000u) == e0 + 2u && as.writes == w0 + 1u);
    tape_reset();                                        /* the power off and on: RAM lost */
    memset(&as, 0, sizeof as);
    {
        int rc = autosave_boot(1);
        ok = rc == 1 && same(nw, 31u);
        if (!ok)
            printf("  boot %d, n %u (want %u), len %u, changed %d\n", rc, tape.n, nw, tape.len, project_changed());
    }
    bad += check("power off and on: the tape as it was", ok);
    as_run(120000u);
    bad += check("two minutes idle after power-on: nothing written (it is what flash holds)", erases_in(0xC8000u, 0xDC000u) == e0 + 2u);
    {   /* a wipe (an empty tape) is autosaved too, and comes back empty */
        tape_reset();
        as_run(30000u);
        ok = erases_in(0xC8000u, 0xDC000u) == e0 + 3u;
        (void)make(20, 1, 0, 32u);
        tape.gen++;
        memset(&as, 0, sizeof as);
        (void)autosave_boot(1);
        bad += check("the tape wiped, idle, power off and on: empty", ok && !tape.n);
    }
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += test_roundtrip();
    bad += test_torn();
    bad += test_quiet();
    bad += test_project();
    bad += test_autosave();
    printf(bad ? "tape_store: %d FAILED\n" : "tape_store: all ok\n", bad);
    return bad ? 1 : 0;
}
