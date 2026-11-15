/* SPDX-License-Identifier: GPL-3.0-only */
/* WAVE (src/eng_wave.c): the wavetable engine, two oscillators with a table each, against the real audio path
 * (mix_block, 32 samples a call):
 *  1. the two tables are built in the part's area a little each call, A's first: playable within 32 calls, both whole
 *     within 600 (0.44 s);
 *  2. every frame of every table, as A's and as B's: no offset, the same power (but where its peaks are clipped),
 *     inside 16 bits; each level holds only its harmonics (64, 32, 16, 8, 4), the lower ones as the level before;
 *  3. a note sounds at its pitch; nothing sounds before the first level is built, and nothing breaks;
 *  4. A's POS, B's POS swept through every table while a note sounds: no step in the sound, the level holds;
 *  5. each oscillator's LEVEL (a square law), its OCT, SEMI and FINE at their pitch; SUB an octave below the note, its
 *     four shapes; NOISE alone, white at COLOUR 127, a rumble at 0;
 *  6. a high note plays a short level (no harmonics past half the sample rate), B two octaves up too; WARP's five kinds:
 *     amount 0 leaves the wave as it is, more adds overtones at the note's pitch; THICK's three copies beat; the filter's
 *     five kinds on white noise; a source around the filter is not filtered; KEY TRK moves the cutoff with the keys;
 *  7. a TABLE changed while a note sounds: it plays on; the new table is whole again soon, the other one untouched;
 *  8. the packed values (the two tables, a WARP's kind and OCT, the filter's kind and what goes through it) there and
 *     back; a factory sound sets its eight and its sixteen more (WV_PX);
 *  9. every preset loads and sounds, none clips the mix; the light tracks (5, 6) cannot have it; its favourites are
 *     refused (row 15 of the favourites is Skiff's settings).
 * Built and run by work/host_tests.sh (as tests/run_tests.sh builds the others). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <math.h>

static int16_t cap[44100 * 8];
static uint32_t cap_n, said;
#define SAY(...) do { if (said++ < 12u) printf(__VA_ARGS__); } while (0)
static void run(uint32_t calls)                      /* calls of 32 samples; the left channel kept */
{
    uint32_t c, i;
    for (c = 0; c < calls; c++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL && cap_n < NELEM(cap); i++)
            cap[cap_n++] = (int16_t)clamp(o[2 * i], -32768, 32767);
    }
}
static void fresh(uint32_t preset)                    /* power-on, track 1 WAVE with a preset, dry */
{
    uint32_t k;
    ui_power_on();
    stop_transport();
    for (k = 0; k < NTRK; k++) {
        track_defaults(&trk[k]);
        trk[k].eng_req = trk[k].engine = 0;
        trk[k].p[P_MUTE] = 0;
    }
    sel_set(0);
    song.master_q12 = 2048;                           /* (main.c felucca_init) */
    set_engine_of(&trk[0], ENGI_WAVE);
    apply_preset_to(&trk[0], preset);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
    cap_n = 0;
}
static double rms_of(const int16_t *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static int32_t peak_of(const int16_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (abs(x[i]) > p)
            p = abs(x[i]);
    return p;
}
/* harmonic k of a cycle of n samples: its level */
static double harm(const int16_t *x, uint32_t n, uint32_t k)
{
    double re = 0, im = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        re += x[i] * cos(6.283185307179586 * k * i / n);
        im += x[i] * sin(6.283185307179586 * k * i / n);
    }
    return 2.0 * sqrt(re * re + im * im) / n;
}
/* the pitch of x by its autocorrelation, Hz */
static double pitch_of(const int16_t *x, uint32_t n, double lo, double hi)
{
    uint32_t l, best = 0, i;
    double bv = -1e30;
    for (l = (uint32_t)(44100.0 / hi); l <= (uint32_t)(44100.0 / lo) && l < n / 2u; l++) {
        double s = 0;
        for (i = 0; i + l < n; i++)
            s += (double)x[i] * x[i + l];
        s /= (double)(n - l);
        if (s > bv) {
            bv = s;
            best = l;
        }
    }
    return best ? 44100.0 / best : 0;
}

static void built(wv_part_t *P)                     /* until nothing is being built and both tables are the ones asked for */
{
    uint32_t c;
    for (c = 0; c < 3000u && (P->stage < WV_NL || P->table[0] != wv_tab(trk[0].p, 0) || P->table[1] != wv_tab(trk[0].p, 1)); c++)
        run(1);
}
static int test_build(void)
{
    int bad = 0;
    wv_part_t *P;
    uint32_t c, first = 0, whole = 0, tb, f, l, n, ok, o, a_first = 1;
    fresh(0);
    run(4);                                           /* (the engine switch: no voices, at once) */
    P = wv_of(&trk[0]);
    printf("wave: a part's tables %u B of the %u B a part has\n", (unsigned)sizeof(wv_part_t), (unsigned)sizeof(eng_mem_t));
    bad += check("track 1 plays WAVE; its tables are being built in the part's area", trk[0].engine == ENGI_WAVE && P &&
                 P->live && sizeof(wv_part_t) <= sizeof(eng_mem_t));
    for (c = 0; c < 2000u && (P->stage < WV_NL || P->ready[1] != 0x1Fu); c++) {
        run(1);
        if (!first && P->ready[0])
            first = c + 5u;
        if (P->ready[1] && P->ready[0] != 0x1Fu)
            a_first = 0;
    }
    whole = c + 4u;
    printf("wave: A plays after %u calls (%u ms); both tables whole after %u (%u ms)\n", first, first * 32u * 1000u / 44100u,
           whole, whole * 32u * 1000u / 44100u);
    bad += check("playable within 32 calls (23 ms), both whole within 600 (0.44 s), A's before B's", first && first <= 32u &&
                 whole <= 600u && P->ready[0] == 0x1Fu && P->ready[1] == 0x1Fu && a_first);
    for (tb = 0, ok = 1; tb < WV_NTAB; tb++) {
        trk[0].p[P_E0] = (int16_t)WVT(tb, (tb + 3u) % WV_NTAB);
        built(P);
        for (o = 0; o < WV_NT; o++) {
            uint32_t clip = 0, t = o ? (tb + 3u) % WV_NTAB : tb;
            if (P->table[o] != t)
                ok = 0;
            for (f = 0; f < WV_NF; f++) {
                const int16_t *x = P->tab[o][f];
                double r = rms_of(x, 256), mean = 0;
                int32_t pk = peak_of(x, 256);
                for (n = 0; n < 256u; n++)
                    mean += x[n];
                mean /= 256.0;
                clip += pk >= 32767;
                if (fabs(mean) > 40.0 || (pk < 32767 && fabs(r - WV_RMS) > WV_RMS * 0.06) || r > WV_RMS * 1.06) {
                    SAY("wave: table %u (as %c) frame %u: mean %.1f rms %.0f peak %d\n", t, o ? 'B' : 'A', f, mean, r, pk);
                    ok = 0;
                }
                for (l = 1; l < WV_NL; l++) {         /* each level: the harmonics it keeps as level 0 has them, none above */
                    uint32_t N = 256u >> l, H = WV_H0 >> l, k;
                    const int16_t *y = x + WV_OFF[l];
                    for (k = 1; k < N / 2u; k++) {
                        double a = harm(y, N, k), a0 = harm(x, 256, k);
                        if (pk >= 32767)
                            continue;                 /* (a clipped frame's are not what was added) */
                        if (k <= H ? fabs(a - a0) > 60.0 + a0 * 0.03 : a > 60.0) {
                            SAY("wave: table %u frame %u level %u harmonic %u: %.1f (level 0: %.1f)\n", t, f, l, k, a, a0);
                            ok = 0;
                        }
                    }
                }
            }
            if (clip > 2u) {
                SAY("wave: table %u: %u frames clipped\n", t, clip);
                ok = 0;
            }
        }
    }
    bad += check("every table, as A's and as B's: no offset, every frame the same power, each level only its harmonics", ok);
    return bad;
}

static int test_note(void)
{
    int bad = 0;
    wv_part_t *P;
    double f;
    fresh(0);
    input_on(&trk[0], 60, 100);                       /* a key before anything is built */
    run(2);
    P = wv_of(&trk[0]);
    bad += check("a note before the first level is whole: silent, nothing breaks", peak_of(cap, cap_n) == 0 && P && !P->ready[0]);
    run(600);
    input_off(&trk[0], 60);
    run(200);
    cap_n = 0;
    input_on(&trk[0], 60, 100);
    run(700);
    f = pitch_of(cap + 8000, 8192, 200, 330);
    printf("wave: C4 measured %.2f Hz, peak %d, rms %.0f\n", f, peak_of(cap + 8000, 8192), rms_of(cap + 8000, 8192));
    bad += check("C4 (INIT SAW) sounds at 261.6 Hz, well inside 16 bits", fabs(f - 261.63) < 2.0 &&
                 peak_of(cap, cap_n) > 3000 && peak_of(cap, cap_n) < 30000);
    input_off(&trk[0], 60);
    run(400);
    bad += check("... and ends when the key goes up", peak_of(cap + cap_n - 2000, 2000) < 40);
    return bad;
}

static int test_pos(void)
{
    int bad = 0;
    uint32_t tb, i, ok = 1, o;
    for (o = 0; o < WV_NT; o++)
        for (tb = 0; tb < WV_NTAB; tb++) {
            double lo = 1e9, hi = 0;
            int32_t jump = 0, big;
            fresh(0);
            trk[0].p[P_E0] = (int16_t)(o ? WVT(0, tb) : WVT(tb, 0));
            trk[0].p[P_E1] = trk[0].p[P_E2] = 0;
            trk[0].p[P_E3] = o ? 127 : 0;             /* (the one swept, alone) */
            trk[0].p[P_E4] = 127;
            run(700);
            input_on(&trk[0], 48, 100);
            run(100);
            cap_n = 0;
            for (i = 0; i < 128u; i++) {              /* POS 0 .. 127, 23 ms a step */
                trk[0].p[o ? P_E2 : P_E1] = (int16_t)i;
                run(32);
            }
            for (i = 1; i < cap_n; i++)
                if (abs(cap[i] - cap[i - 1]) > jump)
                    jump = abs(cap[i] - cap[i - 1]);
            for (i = 0; i + 2048u <= cap_n; i += 2048u) {
                double r = rms_of(cap + i, 2048);
                lo = r < lo ? r : lo;
                hi = r > hi ? r : hi;
            }
            big = tb == 2u ? 30000 : 22000;           /* (PULSE's edges are its sound) */
            if (jump > big || lo < 1500.0 || hi > 12000.0) {
                printf("wave: %c's table %u swept: largest step %d, rms %.0f .. %.0f\n", o ? 'B' : 'A', tb, jump, lo, hi);
                ok = 0;
            }
        }
    bad += check("A's POS, B's POS swept through every table under a held note: it sounds all the way, no step beyond the wave's own", ok);
    return bad;
}

/* the pitch of a sine in x, Hz: its rising zero crossings counted (to a fraction of a sample at both ends) */
static double sine_hz(const int16_t *x, uint32_t n)
{
    double first = -1, last = -1;
    uint32_t i, c = 0;
    for (i = 1; i < n; i++)
        if (x[i - 1] < 0 && x[i] >= 0) {
            double at = (double)(i - 1) + (double)(-x[i - 1]) / (double)(x[i] - x[i - 1]);
            if (first < 0)
                first = at;
            else
                c++;
            last = at;
        }
    return c ? 44100.0 * c / (last - first) : 0;
}
/* the average level of the bins k0 .. k1 of a window of 4096, over four windows from x */
static double band(const int16_t *x, uint32_t k0, uint32_t k1)
{
    double s = 0;
    uint32_t w, k;
    for (w = 0; w < 4u; w++)
        for (k = k0; k <= k1; k += 3u)
            s += harm(x + w * 4096u, 4096, k);
    return s / (4.0 * ((k1 - k0) / 3u + 1u));
}
static void xset(uint32_t k, int v) { trk[0].p[WX(k)] = (int16_t)v; }
static int test_controls(void)
{
    int bad = 0;
    double r[4], a, b, c;
    uint32_t i, ok = 1;
    static const int LV[4] = {127, 90, 0, 127};
    for (i = 0; i < 4u; i++) {                        /* A's LEVEL 127, 90, 0; then B's alone (the same table) */
        fresh(0);
        xset(i == 3u ? WX_BLVL : WX_ALVL, LV[i]);
        if (i == 3u)
            xset(WX_ALVL, 0);
        run(700);
        cap_n = 0;
        input_on(&trk[0], 48, 100);
        run(900);
        r[i] = rms_of(cap + 6000, cap_n - 6000);
    }
    printf("wave: A's LEVEL 127, 90, 0: rms %.0f %.0f %.0f; B alone at 127: %.0f\n", r[0], r[1], r[2], r[3]);
    bad += check("LEVEL: 90 is half of 127 (a square law), 0 silent; B at 127 as loud as A at 127",
                 r[0] > 1500.0 && r[1] > r[0] * 0.45 && r[1] < r[0] * 0.56 && r[2] < 3.0 && fabs(r[3] - r[0]) < r[0] * 0.1);
    {   /* each oscillator alone, a sine (BASIC at POS 0), C6: OCT, SEMI, FINE at their pitch */
        static const int8_t PT[][3] = {{0, 0, 0}, {0, 0, 8}, {0, 0, -22}, {0, 7, 0}, {1, 0, 0}, {1, 7, 0}, {2, 0, 0},
                                       {-1, 0, 0}, {0, -5, 30}, {-3, 0, 0}, {3, 0, 0}, {0, 12, 63}, {0, -12, -63}};
        uint32_t o;
        for (o = 0; o < 2u; o++)
            for (i = 0; i < NELEM(PT); i++) {
                int ct = PT[i][0] * 1200 + PT[i][1] * 100 + PT[i][2];
                double f, want = 1046.502 * pow(2.0, ct / 1200.0);
                fresh(0);
                trk[0].p[P_E0] = (int16_t)WVT(0, 0);
                trk[0].p[P_E1] = trk[0].p[P_E2] = 0;
                xset(WX_ALVL, o ? 0 : 127);
                xset(WX_BLVL, o ? 127 : 0);
                xset(WX_AWARP + o, WVA(WK_OFF, PT[i][0]));
                xset(WX_ASEMI + o, PT[i][1] + 12);
                xset(WX_AFINE + o, PT[i][2] + 64);
                ok &= wv_cents(trk[0].p, o) == ct && wv_cents(trk[0].p, !o) == 0;
                run(700);
                cap_n = 0;
                input_on(&trk[0], 84, 100);
                run(44100u * 4u / 32u);
                f = sine_hz(cap + 4000, cap_n - 4000);
                if (fabs(f - want) > want * 0.0008) {     /* (8 cents are 0.46 %: seen) */
                    printf("wave: %c at %+d cents: %.2f Hz, wanted %.2f\n", o ? 'B' : 'A', ct, f, want);
                    ok = 0;
                }
            }
    }
    bad += check("OCT, SEMI and FINE of A, then of B: each alone at its pitch, 3 octaves down to 3 up, to the cent", ok);
    {   /* SUB: a sine an octave below the note (A a sine too: the two alone); DEEP two below; TRI, SQUARE: overtones */
        double h3[WS_N];
        fresh(0);
        trk[0].p[P_E1] = 0;
        trk[0].p[P_E3] = 127;
        run(700);
        cap_n = 0;
        input_on(&trk[0], 72, 100);                   /* C5, 523.25 Hz: bins 48.6 and 24.3 of 4096 */
        run(600);
        a = harm(cap + 8192, 8192, 97);
        b = harm(cap + 8192, 8192, 49);
        printf("wave: SUB: the note %.0f, an octave below it %.0f\n", a, b);
        bad += check("SUB all the way: a sine an octave below A, as loud as A or louder", a > 1000.0 && b > a * 0.8);
        trk[0].p[P_E3] = 0;
        input_off(&trk[0], 72);
        run(300);
        cap_n = 0;
        input_on(&trk[0], 72, 100);
        run(600);
        bad += check("SUB at 0: nothing an octave below", harm(cap + 8192, 8192, 49) < harm(cap + 8192, 8192, 97) / 50.0);
        for (i = 0; i < WS_N; i++) {
            fresh(0);
            xset(WX_ALVL, 0);
            trk[0].p[P_E3] = 127;
            xset(WX_SUBK, (int)i);
            run(700);
            cap_n = 0;
            input_on(&trk[0], 72, 100);
            run(600);
            a = harm(cap + 8192, 8192, i == WS_DEEP ? 24 : 49);   /* its own pitch: 261.6 Hz, DEEP's 130.8 */
            h3[i] = harm(cap + 8192, 8192, i == WS_DEEP ? 73 : 146) / (a > 1.0 ? a : 1.0);
            printf("wave: SUB %-6s its note %.0f, its third harmonic %.3f of it\n", N_WV_SUB[i], a, h3[i]);
            ok &= a > 3000.0;
        }
        bad += check("SUB's shapes alone: SINE and DEEP (an octave lower) pure, TRI a ninth of it at the third harmonic, SQUARE more",
                     ok && h3[WS_SINE] < 0.02 && h3[WS_DEEP] < 0.02 && h3[WS_TRI] > 0.08 && h3[WS_TRI] < 0.14 &&
                     h3[WS_SQUARE] > 0.2);
    }
    {   /* NOISE alone: at COLOUR 127 as much up high as down low, at 0 a rumble; no LEVEL: silent */
        double lo, hi, lo0, hi0;
        fresh(0);
        xset(WX_ALVL, 0);
        xset(WX_NLVL, 127);
        xset(WX_NCOL, 127);
        trk[0].p[P_E4] = 127;
        run(700);
        cap_n = 0;
        input_on(&trk[0], 60, 100);
        run(900);
        a = rms_of(cap + 4000, 20000);
        lo = band(cap + 4000, 20, 60);
        hi = band(cap + 4000, 700, 1100);
        input_off(&trk[0], 60);
        xset(WX_NCOL, 0);
        run(300);
        cap_n = 0;
        input_on(&trk[0], 60, 100);
        run(900);
        lo0 = band(cap + 4000, 20, 60);
        hi0 = band(cap + 4000, 700, 1100);
        printf("wave: NOISE rms %.0f; low and high bands at COLOUR 127: %.1f %.1f, at 0: %.1f %.1f\n", a, lo, hi, lo0, hi0);
        bad += check("NOISE alone: it sounds; COLOUR 127 white (high as low), COLOUR 0 a rumble (a twentieth up high)",
                     a > 1500.0 && hi > lo * 0.5 && hi < lo * 2.0 && hi0 < lo0 / 20.0 && lo0 > 5.0);
    }
    return bad;
}

/* a note of the sound as it is: its capture from sample 2000 on (cap), 900 calls */
static void play(uint32_t note)
{
    run(700);
    cap_n = 0;
    input_on(&trk[0], note, 100);
    run(900);
}
static int test_levels(void)
{
    int bad = 0;
    double a, b, base, fund;
    uint32_t i, pass, k, ok;
    for (pass = 0; pass < 2u; pass++) {               /* A at C8; B two octaves above C6 */
        fresh(0);
        if (pass) {
            xset(WX_ALVL, 0);
            xset(WX_BLVL, 127);
            xset(WX_BWARP, WVA(WK_OFF, 2));
        }
        run(700);
        cap_n = 0;
        input_on(&trk[0], pass ? 84 : 108, 100);      /* C8, 4186 Hz: 4 harmonics is all there is room for */
        run(300);
        a = harm(cap + 4096, 4096, 389);              /* the note itself */
        for (b = 0, i = 500; i < 2040u; i++)          /* what is not on a harmonic of it */
            if (i % 389u > 12u && i % 389u < 377u) {
                double h = harm(cap + 4096, 4096, i);
                b = h > b ? h : b;
            }
        printf("wave: C8 from %s: the note %.0f, the loudest thing off its harmonics %.0f\n", pass ? "B, 2 octaves up" : "A", a, b);
        bad += check(pass ? "B two octaves up at C8: the same" : "C8: what is off the note's harmonics is under a fortieth of it",
                     a > 2000.0 && b < a / 40.0);
        input_off(&trk[0], pass ? 84 : 108);
    }
    /* WARP, A a sine at C4 (bin 48.6 of 8192): every kind at amount 0 is the sine as it is; at 64 and 127 it sounds at
     * the note's pitch, inside 16 bits, with overtones the sine has not (its harmonics 2 .. 12 together) */
#define OVER(x) (harm(x, 8192, 97) + harm(x, 8192, 146) + harm(x, 8192, 194) + harm(x, 8192, 243) + harm(x, 8192, 292) + \
                 harm(x, 8192, 340) + harm(x, 8192, 389) + harm(x, 8192, 437) + harm(x, 8192, 486) + harm(x, 8192, 535) + \
                 harm(x, 8192, 583))
    fresh(0);
    trk[0].p[P_E1] = 0;
    play(60);
    base = OVER(cap + 8192);
    fund = harm(cap + 8192, 8192, 49);
    for (k = WK_SYNC, ok = 1; k < WK_N; k++) {
        static const int AM[3] = {0, 64, 127};
        for (i = 0; i < 3u; i++) {
            double f, h, fu;
            fresh(0);
            trk[0].p[P_E1] = 0;
            xset(WX_AWARP, WVA(k, 0));
            trk[0].p[P_E6] = (int16_t)AM[i];
            if (k == WK_FM) {                         /* (by B: a sine an octave up, all but silent itself) */
                trk[0].p[P_E2] = 0;
                xset(WX_BWARP, WVA(WK_OFF, 1));
                xset(WX_BLVL, 1);
            }
            play(60);
            h = OVER(cap + 8192);
            fu = harm(cap + 8192, 8192, 49);
            if (!i) {
                if (h > base + 60.0 || fabs(fu - fund) > fund * 0.01) {
                    printf("wave: WARP %s at 0: not the sine as it is (overtones %.0f, the sine's %.0f)\n", N_WV_KIND[k], h, base);
                    ok = 0;
                }
                continue;
            }
            f = pitch_of(cap + 8192, 8192, 200.0, 400.0);
            printf("wave: WARP %-5s at %3d: %.1f Hz, peak %d, overtones %.0f (the sine's %.0f)\n", N_WV_KIND[k], AM[i],
                   f, peak_of(cap, cap_n), h, base);
            if (fabs(f - 261.63) > 3.0 || peak_of(cap + 2000, 8000) < 1500 || peak_of(cap, cap_n) > 30000 || h < base + 1000.0)
                ok = 0;
        }
    }
    bad += check("WARP's kinds (SYNC BEND+ BEND- PWM FM): amount 0 leaves the wave as it is; more adds overtones at the note's pitch",
                 ok);
    {   /* THICK: three of it a little apart beat against each other; none: steady */
        double lo[2] = {1e9, 1e9}, hi[2] = {0, 0};
        for (pass = 0; pass < 2u; pass++) {
            fresh(0);
            trk[0].p[P_E1] = 0;
            xset(WX_ATHICK, pass ? 127 : 0);
            play(72);
            for (i = 0; i < 20u; i++) {               /* 20 windows of 23 ms */
                double r = rms_of(cap + 4000 + i * 1024u, 1024);
                lo[pass] = r < lo[pass] ? r : lo[pass];
                hi[pass] = r > hi[pass] ? r : hi[pass];
            }
        }
        printf("wave: THICK 0: rms %.0f .. %.0f; THICK 127: %.0f .. %.0f\n", lo[0], hi[0], lo[1], hi[1]);
        bad += check("THICK: none a steady note; all the way three copies beating (its level moving by a third and more)",
                     hi[0] < lo[0] * 1.03 && hi[1] > lo[1] * 1.33 && hi[1] < 30000.0 && lo[0] > 1000.0);
    }
    {   /* THICK and the notes at once: 8 as it is, 6 with one oscillator THICK, 4 with both */
        static const uint8_t N[8] = {36, 43, 48, 52, 55, 60, 64, 67};
        uint32_t got[3], want[3] = {8, 6, 4};
        for (pass = 0; pass < 3u; pass++) {
            fresh(0);
            trk[0].p[P_VOICE] = V_POLY;
            xset(WX_BLVL, 100);
            xset(WX_ATHICK, pass ? 60 : 0);
            xset(WX_BTHICK, pass > 1u ? 60 : 0);
            run(700);
            for (i = 0; i < 8u; i++) {
                input_on(&trk[0], N[i], 100);
                run(4);
            }
            run(40);
            for (i = 0, got[pass] = 0; i < NVOICE; i++)
                got[pass] += trk[0].v[i].active && trk[0].v[i].gate;
        }
        printf("wave: 8 keys held: %u notes sound; A THICK: %u; both THICK: %u\n", got[0], got[1], got[2]);
        bad += check("eight keys held: 8 notes as it is, 6 with one oscillator THICK, 4 with both (the latest ones)",
                     got[0] == want[0] && got[1] == want[1] && got[2] == want[2]);
    }
    {   /* the filter's kinds on white noise, CUTOFF at 1.9 kHz (bin 176 of 4096): what each lets through */
        double lo[WF_N], mid[WF_N], hi[WF_N];
        for (k = 0; k < WF_N; k++) {
            fresh(0);
            xset(WX_ALVL, 0);
            xset(WX_NLVL, 127);
            xset(WX_NCOL, 127);
            xset(WX_KTRK, 0);
            xset(WX_FLT, (int)WVF(k, WR_ALL));
            trk[0].p[P_E4] = 84;
            trk[0].p[P_E5] = 0;
            play(60);
            lo[k] = band(cap + 4000, 15, 45);
            mid[k] = band(cap + 4000, 150, 200);
            hi[k] = band(cap + 4000, 800, 1200);
            printf("wave: filter %-6s low %.1f, at the cutoff %.1f, high %.1f\n", N_WV_FLT[k], lo[k], mid[k], hi[k]);
        }
        bad += check("LOW 12 cuts the highs, LOW 24 harder; HIGH cuts the lows; BAND keeps the middle; NOTCH takes it out",
                     hi[WF_LOW] < lo[WF_LOW] / 8.0 && hi[WF_LOW24] < hi[WF_LOW] / 3.0 && lo[WF_LOW24] > lo[WF_LOW] * 0.7 &&
                     lo[WF_HIGH] < hi[WF_HIGH] / 5.0 && lo[WF_BAND] < mid[WF_BAND] / 2.0 && hi[WF_BAND] < mid[WF_BAND] / 2.0 &&
                     mid[WF_NOTCH] < lo[WF_NOTCH] * 0.6 && mid[WF_NOTCH] < hi[WF_NOTCH] * 0.6);
    }
    {   /* what goes through it: the SUB alone, the filter shut: through it nearly gone, around it as it is */
        double thr, ard;
        for (pass = 0; pass < 2u; pass++) {
            fresh(0);
            xset(WX_ALVL, 0);
            trk[0].p[P_E3] = 127;
            trk[0].p[P_E4] = 10;
            xset(WX_KTRK, 0);
            xset(WX_FLT, (int)WVF(WF_LOW, pass ? WR_ALL & ~WR_SUB : WR_ALL));
            play(72);
            if (pass)
                ard = rms_of(cap + 6000, 16000);
            else
                thr = rms_of(cap + 6000, 16000);
        }
        printf("wave: the SUB with the filter shut: through it rms %.0f, around it %.0f\n", thr, ard);
        bad += check("a source switched around the filter is not filtered (the SUB, the filter shut: 5 times the level)",
                     ard > thr * 5.0 && ard > 2000.0);
    }
    {   /* KEY TRK: a saw at C7 under a low CUTOFF: none dull, all the way brighter */
        double d0, d1;
        for (pass = 0; pass < 2u; pass++) {
            fresh(0);
            trk[0].p[P_E4] = 60;
            xset(WX_KTRK, pass ? 127 : 0);
            play(96);
            if (pass)
                d1 = rms_of(cap + 6000, 16000);
            else
                d0 = rms_of(cap + 6000, 16000);
        }
        printf("wave: C7 under CUTOFF 60: KEY TRK 0 rms %.0f, 127 rms %.0f\n", d0, d1);
        bad += check("KEY TRK: the cutoff follows the keys (C7 louder through a low CUTOFF with it than without)", d1 > d0 * 1.5);
    }
    return bad;
}

static int test_change(void)
{
    int bad = 0;
    wv_part_t *P;
    uint32_t c;
    static int16_t keep[WV_NF][WV_FS];
    fresh(0);
    run(700);
    P = wv_of(&trk[0]);
    memcpy(keep, P->tab[1], sizeof keep);
    input_on(&trk[0], 55, 100);
    run(100);
    cap_n = 0;
    trk[0].p[P_E0] = (int16_t)WVT(3, 0);              /* A's FORMANT, the note held */
    run(40);
    bad += check("A's TABLE turned under a held note: it plays on while the new one is built",
                 P->table[0] == 3u && P->bt == 0u && P->stage < WV_NL && rms_of(cap, cap_n) > 1500.0);
    for (c = 0; c < 1000u && P->stage < WV_NL; c++)
        run(1);
    bad += check("... whole again within 300 calls, B's table as it was", c <= 300u && P->ready[0] == 0x1Fu &&
                 !memcmp(keep, P->tab[1], sizeof keep));
    memcpy(keep, P->tab[0], sizeof keep);
    trk[0].p[WX(WX_ALVL)] = 0;                        /* B alone; its table turned */
    trk[0].p[WX(WX_BLVL)] = 127;
    run(40);
    cap_n = 0;
    trk[0].p[P_E0] = (int16_t)WVT(3, 6);
    run(40);
    bad += check("B's TABLE turned under it: it plays on", P->table[1] == 6u && P->bt == 1u && P->stage < WV_NL &&
                 rms_of(cap, cap_n) > 1000.0);
    for (c = 0; c < 1000u && P->stage < WV_NL; c++)
        run(1);
    bad += check("... whole again within 300 calls, A's table as it was", c <= 300u && P->ready[1] == 0x1Fu &&
                 !memcmp(keep, P->tab[0], sizeof keep));
    trk[0].p[P_E0] = (int16_t)WVT(5, 2);              /* both at once (a preset): A's first, then B's */
    run(2);
    c = P->bt == 0u && P->table[0] == 5u;
    built(P);
    bad += check("both turned at once: A's built first, then B's", c && P->table[0] == 5u && P->table[1] == 2u &&
                 P->ready[0] == 0x1Fu && P->ready[1] == 0x1Fu);
    return bad;
}

static int test_pack(void)
{
    int bad = 0;
    int16_t p[P_COUNT];
    uint32_t a, b, ok = 1;
    int o;
    memset(p, 0, sizeof p);
    for (a = 0; a < 8u; a++)
        for (b = 0; b < 8u; b++) {
            p[P_E0] = (int16_t)WVT(a, b);
            ok &= wv_tab(p, 0) == a && wv_tab(p, 1) == b && p[P_E0] <= 63;
        }
    for (a = 0; a < WK_N; a++)
        for (o = -3; o <= 3; o++) {
            p[WX(WX_AWARP)] = (int16_t)WVA(a, o);
            p[WX(WX_BWARP)] = (int16_t)WVA(WK_N - 1u - a, -o);
            ok &= wv_kind(p, 0) == a && wv_oct(p, 0) == o && wv_kind(p, 1) == WK_N - 1u - a && wv_oct(p, 1) == -o &&
                  p[WX(WX_AWARP)] <= 127;
        }
    for (a = 0; a < WF_N; a++)
        for (b = 0; b < 16u; b++) {
            p[WX(WX_FLT)] = (int16_t)WVF(a, b);
            ok &= wv_fkind(p) == a && wv_through(p) == b && p[WX(WX_FLT)] <= 127;
        }
    bad += check("the two tables in E0, a WARP's kind and OCT, the filter's kind and what goes through it: there and back, 0 .. 127", ok);
    for (a = 0, ok = 1; a < 8u; a++) {
        const param_desc_t *d = &ENG_WAVE.edit[a];
        ok &= d->min == 0 && d->max <= 127 && d->def >= d->min && d->def <= d->max;
    }
    for (a = 0; a < ENG_WAVE.npresets; a++) {
        for (b = 0; b < 8u; b++)
            ok &= ENG_WAVE.presets[a].e[b] <= ENG_WAVE.edit[b].max && ENG_WAVE.presets[a].e[b] >= 0;
        for (b = 0; b < WX_N; b++)
            ok &= WV_PX[a][b] <= 127;
        ok &= (WV_PX[a][WX_AWARP] & 7u) < WK_N && (WV_PX[a][WX_BWARP] & 7u) < WK_N && (WV_PX[a][WX_FLT] & 7u) < WF_N &&
              WV_PX[a][WX_SUBK] < WS_N && WV_PX[a][WX_ASEMI] <= 24 && WV_PX[a][WX_BSEMI] <= 24 && WV_PX[a][WX_AFINE] >= 1 &&
              WV_PX[a][WX_BFINE] >= 1 && (WV_PX[a][WX_ALVL] || WV_PX[a][WX_BLVL]);
    }
    bad += check("its values' ranges hold every factory sound's: the eight, and the sixteen more", ok);
    {   /* the sixteen more: a factory sound sets them all; another engine's sound leaves no WAVE values behind */
        fresh(1);
        ok = trk[0].p[WX(WX_ATHICK)] == WV_PX[1][WX_ATHICK] && trk[0].p[WX(WX_BLVL)] == WV_PX[1][WX_BLVL];
        trk[0].p[WX(WX_NLVL)] = 99;
        apply_preset_to(&trk[0], 0);
        for (b = 0; b < WX_N; b++)
            ok &= trk[0].p[WX(b)] == WV_PX[0][b];
        bad += check("a factory sound sets all sixteen (SUPERSAW's THICK and B, then INIT SAW's over a NOISE turned up)", ok);
    }
    return bad;
}

static int test_presets(void)
{
    int bad = 0;
    uint32_t p, ok = 1, row, i;
    uint8_t before[32];
    for (p = 0; p < ENG_WAVE.npresets; p++) {
        int32_t pk;
        double r;
        fresh(p);
        trk[0].p[P_CHOR] = ENG_WAVE.presets[p].fx[1] ? ENG_WAVE.presets[p].fx[1] - 1 : 0;   /* (its own chorus: SUPERSAW's) */
        run(700);
        cap_n = 0;
        input_on(&trk[0], 48, 110);
        input_on(&trk[0], 55, 110);
        input_on(&trk[0], 60, 110);
        run(600);
        pk = peak_of(cap, cap_n);
        r = rms_of(cap + 2000, cap_n - 2000);
        printf("wave: %-12s peak %5d rms %5.0f\n", ENG_WAVE.presets[p].name, pk, r);
        if (pk < 1500 || pk > 32000 || r < 300.0)
            ok = 0;
        input_off(&trk[0], 48);
        input_off(&trk[0], 55);
        input_off(&trk[0], 60);
    }
    bad += check("every preset: three notes sound, the mix does not clip", ok);
    bad += check("WAVE is the 16th engine, shown second; tracks 1..4 may have it, 5 and 6 not",
                 NENGINES == 16u && ENGINES[ENGI_WAVE] == &ENG_WAVE && ENGINE_ORDER[1] == ENGI_WAVE && eng_allowed(0, ENGI_WAVE) &&
                 eng_allowed(3, ENGI_WAVE) && !eng_allowed(4, ENGI_WAVE) && !eng_allowed(5, ENGI_WAVE) && !wv_of(&trk[4]));
    for (row = 0; row < 16u; row++)
        for (i = 0; i < 32u; i++)
            favorites.factory[row][i] = (uint8_t)(row * 7u + i);
    memcpy(before, favorites.factory[15], 32);
    bad += check("its favourites: refused (row 15 is Skiff's settings), that row untouched",
                 !favorite_set(ENGI_WAVE, 0, 1) && !favorite_has(ENGI_WAVE, 0) && !memcmp(before, favorites.factory[15], 32));
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += test_build();
    bad += test_note();
    bad += test_pos();
    bad += test_controls();
    bad += test_levels();
    bad += test_change();
    bad += test_pack();
    bad += test_presets();
    printf(bad ? "wave: %d FAILED\n" : "wave: all ok\n", bad);
    return bad != 0;
}
