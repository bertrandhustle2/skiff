/* SPDX-License-Identifier: GPL-3.0-only */
/* The new layout's EDIT and SAVE (firmware/src/ui_design.c, ui_save2.c), through the buttons, the knobs and the audio's
 * block loop (tape_test.c's own harness):
 *  1. EDIT tapped on a track with another kind of sound: the designer's sound is tried there, the track's own kept
 *     aside; its pages OSC A and OSC B, FILTER under ENV, each knob its one control; HOME (a SEQ or SAVE page, another
 *     track) puts the track's own sound back exactly; the designer's sound is what was left;
 *  2. EDIT held: the sound kept on the track, SAVE held undoes it (the track's own back), OCT+ redoes it; EDIT held on
 *     another track writes the same sound there; a track that has it says so; nothing designed yet says so;
 *  3. a WAVE track: EDIT edits its own sound, and that is the designer's from then on; tracks 5 and 6 cannot;
 *  4. PRESETS steps WAVE's sounds on the designer's pages; what the trial loaded leaves nothing to undo; a take made
 *     before it is still what SAVE held takes back; an FM6 track's patch and a track's automation come back whole;
 *  5. what EDIT held before: under FX (ANALOG's OSC, FM6's OPERATOR, VOICE), a filter's page under ENV; WAVE's own two
 *     pages are not shown; on NOTES, armed, EDIT still clears a step;
 *  6. SAVE: PROJECT SAVE, SOUND SAVE, then the list; a project kept with its tape and loaded back; a track's sound
 *     kept in a slot and loaded onto another track; PATTERNS is under SEQ.
 * Built and run by work/host_tests.sh. */
#define TAPE_TEST_NO_MAIN 1
#include "tape_test.c"

static uint32_t cur_graph(void) { return ui.home ? 0xFFFFu : cur_page()->graph; }
static int16_t snap_p[NTRK][P_COUNT];
static uint8_t snap_e[NTRK], snap_pr[NTRK], snap_u[NTRK];
static void snap(void)                               /* every track's sound, as it is */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        memcpy(snap_p[k], trk[k].p, sizeof snap_p[k]);
        snap_e[k] = trk[k].eng_req;
        snap_pr[k] = trk[k].preset;
        snap_u[k] = trk[k].user;
    }
}
static int same_as_snap(uint32_t k)                  /* track k's sound is what snap() saw */
{
    return trk[k].eng_req == snap_e[k] && trk[k].preset == snap_pr[k] && trk[k].user == snap_u[k] &&
           !memcmp(trk[k].p, snap_p[k], sizeof snap_p[k]);
}
static void new_layout(void)
{
    fresh();
    tape_power_on();
    go_home();
    frame();
}
static void settle(void) { uint32_t i; for (i = 0; i < 8u; i++) { frame(); block(); } }

static int test_trial2(void)
{
    int bad = 0;
    int16_t e[8], e2[8];
    uint32_t i, ok;
    new_layout();
    snap();
    press(B_EDIT);
    settle();
    bad += check("EDIT on an ANALOG track: the designer's sound tried there (WAVE), its SOUND page, said how to keep it",
                 design_trk == 1u && trk[0].eng_req == ENGI_WAVE && trk[0].engine == ENGI_WAVE && cur_graph() == GR_DSND &&
                 msg_is("DESIGNING FOR T1") && !strcmp(ui.msg2, "HOLD [EDIT]: SEND TO T1") && ds.c.eng == snap_e[0]);
    bad += check("the other tracks as they were", same_as_snap(1) && same_as_snap(2) && same_as_snap(5));
    memcpy(e, &trk[0].p[P_E0], sizeof e);
#define X(k) trk[0].p[WX(k)]
    turn(EN_K1, 3);                                  /* WAVE A: its table alone */
    ok = wv_tab(trk[0].p, 0) == 3u && wv_tab(trk[0].p, 1) == (uint32_t)(e[0] >> 3) && design_hot() == 0;
    turn(EN_K2, 6);                                  /* WAVE B */
    ok &= wv_tab(trk[0].p, 1) == 6u && wv_tab(trk[0].p, 0) == 3u && design_hot() == 1;
    turn(EN_K3, -30);                                /* A LEVEL */
    ok &= X(WX_ALVL) < 127 && X(WX_ALVL) > 0;
    turn(EN_K4, 40);                                 /* B LEVEL (INIT SAW: none) */
    ok &= X(WX_BLVL) > 0;
    ok &= trk[0].p[P_E1] == e[1] && trk[0].p[P_E2] == e[2] && trk[0].p[P_E4] == e[4] && trk[0].p[P_E5] == e[5];
    bad += check("SOUND: WAVE A, WAVE B, A LEVEL, B LEVEL, each knob its one control; the knob turned last is the strip's", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DOSCA && ds.win[1] == 0u && design_trk == 1u && design_hot() < 0;
    turn(EN_K1, -20);                                /* POS */
    ok &= trk[0].p[P_E1] < e[1];
    turn(EN_K2, 2);                                  /* WARP: OFF, SYNC, BEND+ */
    ok &= wv_kind(trk[0].p, 0) == WK_BENDP && wv_oct(trk[0].p, 0) == 0 && wv_kind(trk[0].p, 1) == WK_OFF;
    turn(EN_K3, 30);                                 /* AMOUNT */
    ok &= trk[0].p[P_E6] > 0;
    i = (uint32_t)X(WX_ALVL);
    turn(EN_K4, 5);                                  /* (A) LEVEL again */
    ok &= (uint32_t)X(WX_ALVL) > i;
    bad += check("EDIT again, OSC A: POS, WARP (its kinds one by one), AMOUNT, LEVEL", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DOSCA && ds.win[1] == 1u && design_hot() < 0;
    turn(EN_K1, 2);                                  /* OCTAVE: the WARP's kind stays */
    ok &= wv_oct(trk[0].p, 0) == 2 && wv_kind(trk[0].p, 0) == WK_BENDP;
    turn(EN_K1, 9);
    ok &= wv_oct(trk[0].p, 0) == 3;                  /* (its end) */
    turn(EN_K2, -5);                                 /* SEMI */
    ok &= wv_semi(trk[0].p, 0) == -5;
    turn(EN_K2, -20);
    ok &= wv_semi(trk[0].p, 0) == -12;               /* (its end) */
    turn(EN_K3, 6);                                  /* FINE */
    ok &= wv_fine(trk[0].p, 0) > 0 && wv_fine(trk[0].p, 0) <= 63;
    turn(EN_K4, 40);                                 /* THICK */
    ok &= X(WX_ATHICK) > 0 && wv_cents(trk[0].p, 1) == 0;
    bad += check("EDIT again, OSC A's second row: OCTAVE, SEMI, FINE (to their ends), THICK; B's pitch as it was", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DOSCB && ds.win[2] == 0u;
    turn(EN_K1, -30);                                /* B's POS */
    ok &= trk[0].p[P_E2] < e[2];
    turn(EN_K2, 9);                                  /* B's WARP: its last kind */
    ok &= wv_kind(trk[0].p, 1) == WK_FM && wv_kind(trk[0].p, 0) == WK_BENDP;
    turn(EN_K3, 25);                                 /* B's AMOUNT: its own value */
    ok &= X(WX_BAMT) > 0;
    press(B_EDIT);
    ok &= cur_graph() == GR_DOSCB && ds.win[2] == 1u;
    turn(EN_K2, 7);
    ok &= wv_semi(trk[0].p, 1) == 7 && wv_semi(trk[0].p, 0) == -12;
    bad += check("OSC B's two rows: its own POS, WARP, AMOUNT, SEMI", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DSUB;
    turn(EN_K1, 30);                                 /* SUB */
    ok &= trk[0].p[P_E3] > 0;
    turn(EN_K2, 2);                                  /* SHAPE: SINE, DEEP, TRI */
    ok &= wv_subk(trk[0].p) == WS_TRI;
    turn(EN_K3, 30);                                 /* NOISE */
    ok &= X(WX_NLVL) > 0;
    turn(EN_K4, -40);                                /* COLOUR */
    ok &= X(WX_NCOL) < 127;
    bad += check("SUB NOISE: SUB, SHAPE, NOISE, COLOUR", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DFLT && ds.win[4] == 0u;
    turn(EN_K1, -40);
    ok &= trk[0].p[P_E4] < e[4];
    turn(EN_K2, 9);
    ok &= trk[0].p[P_E5] > e[5];
    turn(EN_K3, 20);
    ok &= trk[0].p[P_E7] > 0;
    turn(EN_K4, 1);                                  /* KIND: LOW 24; everything still goes through it */
    ok &= wv_fkind(trk[0].p) == WF_LOW24 && wv_through(trk[0].p) == WR_ALL;
    press(B_EDIT);
    ok &= cur_graph() == GR_DFLT && ds.win[4] == 1u;
    turn(EN_K4, -1);                                 /* SUB around it */
    ok &= wv_through(trk[0].p) == (WR_ALL & ~WR_SUB);
    turn(EN_K4, -3);
    turn(EN_K1, -1);                                 /* WAVE A around it, and back */
    ok &= wv_through(trk[0].p) == (WR_B | WR_NOISE);
    turn(EN_K1, 2);
    ok &= wv_through(trk[0].p) == (WR_ALL & ~WR_SUB) && wv_fkind(trk[0].p) == WF_LOW24;
    press(B_EDIT);
    ok &= cur_graph() == GR_DFLT && ds.win[4] == 2u;
    turn(EN_K1, 12);                                 /* ENV AMT */
    ok &= trk[0].p[P_ED_FLT] > snap_p[0][P_ED_FLT] || trk[0].p[P_ED_FLT] > 0;
    i = (uint32_t)X(WX_KTRK);
    turn(EN_K2, -30);                                /* KEY TRK */
    ok &= (uint32_t)X(WX_KTRK) < i;
    memcpy(e2, &trk[0].p[P_E0], sizeof e2);
    turn(EN_K3, 5);                                  /* (no control there) */
    turn(EN_K4, -5);
    ok &= !memcmp(e2, &trk[0].p[P_E0], sizeof e2);
    bad += check("FILTER's three rows: CUTOFF RES DRIVE KIND; what goes through it, each source; ENV AMT, KEY TRK", ok);
    press(B_EDIT);
    ok = cur_graph() == GR_DSND && design_trk == 1u;
    turn(EN_SELECT, -1);
    ok &= cur_graph() == GR_DSND;                    /* (SELECT: the first page is its end) */
    turn(EN_SELECT, 2);
    ok &= cur_graph() == GR_DOSCA && ds.win[1] == 1u;
    turn(EN_SELECT, -1);
    ok &= cur_graph() == GR_DOSCA && ds.win[1] == 0u;
    turn(EN_SELECT, 7);
    ok &= cur_graph() == GR_DFLT && ds.win[4] == 2u;
    turn(EN_SELECT, 3);
    ok &= cur_graph() == GR_DFLT && ds.win[4] == 2u; /* (its other end) */
    turn(EN_SELECT, -2);
    ok &= cur_graph() == GR_DFLT && ds.win[4] == 0u;
    bad += check("EDIT once more: SOUND again (nine screens round); SELECT steps the same nine both ways, its ends where they are", ok);
#undef X
    press(B_ENV);
    ok = cur_graph() == GR_ADSR && design_trk == 1u;
    press(B_ENV);
    ok &= design_trk == 1u && !GR_DESIGN(cur_graph());
    bad += check("ENV, ENV 2: Skiff's own, on the designer's sound; the trial lasts there", ok);
    press(B_LFO);
    ok = design_trk == 1u;
    press(B_FX);
    ok &= design_trk == 1u;
    press(B_SCL);
    ok &= design_trk == 1u;
    press(B_ARP);
    ok &= design_trk == 1u;
    press(B_GLO);
    ok &= design_trk == 1u && trk[0].eng_req == ENGI_WAVE;
    bad += check("it lasts on LFO, FX, SCL, ARP and the mixer", ok);
    memcpy(e2, &trk[0].p[P_E0], sizeof e2);
    press(B_HOME);
    settle();
    bad += check("HOME: the track's own sound back, every value, said so", !design_trk && same_as_snap(0) && trk[0].engine == snap_e[0] &&
                 msg_is("T1: ITS OWN SOUND BACK"));
    {   /* the strip is gone with the designer's page: HOME's cards as they were before it, to the pixel */
        static uint16_t band[44 * 240];
        uint32_t same;
        ui.msg_t = 0;
        ui.force = 1;
        frame();
        memcpy(band, &host_screen[28 * 240], sizeof band);
        press(B_EDIT);
        settle();
        same = !memcmp(band, &host_screen[28 * 240], sizeof band);
        press(B_HOME);
        settle();
        ui.msg_t = 0;
        frame();
        frame();
        bad += check("the designer's strip covers the cards' place; back on HOME the cards are as before it, to the pixel",
                     !same && !memcmp(band, &host_screen[28 * 240], sizeof band));
    }
    press(B_EDIT);
    settle();
    bad += check("EDIT again: the designer's sound as it was left", design_trk == 1u && !memcmp(e2, &trk[0].p[P_E0], sizeof e2) &&
                 trk[0].p[P_ED_FLT] != snap_p[0][P_ED_FLT]);
    press(B_SEQ);
    settle();
    bad += check("a SEQ page: the trial over, the track's own sound", !design_trk && same_as_snap(0));
    press(B_EDIT);
    press(B_SAVE);
    settle();
    bad += check("a SAVE page: the same", !design_trk && same_as_snap(0));
    press(B_EDIT);
    sel_set(1);
    frame();
    settle();
    bad += check("another track picked: track 1's own sound back, track 2 untouched", !design_trk && same_as_snap(0) && same_as_snap(1));
    for (i = 0, ok = 1; i < UNDO_N; i++)
        ok &= !undo_n;
    bad += check("none of it left anything to undo", ok);
    return bad;
}

/* SHIFT + EDIT on the designer's pages: its sound from scratch, WAVE's first (a plain saw, nothing on it) */
static int test_fresh(void)
{
    int bad = 0;
    uint32_t ok;
    new_layout();
    snap();
    press(B_EDIT);                                   /* (track 1, ANALOG: the designer's sound tried there) */
    settle();
    turn(EN_PRESET, 4);                              /* another of WAVE's sounds, then its knobs turned */
    turn(EN_K4, 40);
    press(B_EDIT);
    turn(EN_K1, -30);
    settle();
    ok = design_trk == 1u && (trk[0].preset != 0u || trk[0].p[P_E1] != 127);
    btn_down(B_HOME);
    frame();
    press(B_EDIT);
    btn_up(B_HOME);
    settle();
    bad += check("SHIFT + EDIT there: a plain saw (INIT SAW: A alone, the filter open, no sub, noise or effects), still tried on T1",
                 ok && design_trk == 1u && trk[0].eng_req == ENGI_WAVE && trk[0].preset == 0u && trk[0].p[P_E1] == 127 &&
                 trk[0].p[WX(WX_ALVL)] == 127 && trk[0].p[WX(WX_BLVL)] == 0 && trk[0].p[P_E3] == 0 &&
                 trk[0].p[WX(WX_NLVL)] == 0 && trk[0].p[P_E4] == 127 && msg_is("PLAIN SAW ON T1") && !ui.home && !ui.menu &&
                 GR_DESIGN(cur_graph()) && same_as_snap(1));
    hold_btn(B_EDIT, 1);
    settle();
    bad += check("EDIT held: T1 has the plain saw", !design_trk && trk[0].eng_req == ENGI_WAVE && trk[0].preset == 0u &&
                 msg_is("T1 HAS THE NEW SOUND"));
    turn(EN_K1, 3);                                  /* (its own sound now: changed in place) */
    turn(EN_PRESET, 5);
    settle();
    btn_down(B_HOME);
    frame();
    press(B_EDIT);
    btn_up(B_HOME);
    settle();
    bad += check("SHIFT + EDIT on a track's own WAVE sound: the plain saw there, said how to undo",
                 !design_trk && trk[0].preset == 0u && trk[0].p[P_E1] == 127 && !strcmp(ui.msg2, "HOLD [SAVE]: UNDO") && !ui.menu);
    turn(EN_PRESET, 3);                              /* HOME held until SHIFT's list is up (past the MENU's time too), */
    settle();                                        /* then EDIT: the plain saw, and no MENU when HOME is let go */
    btn_down(B_HOME);
    { uint32_t k; for (k = 0; k < 70u; k++) frame(); }
    ok = ui.shift && !ui.menu;
    press(B_EDIT);
    btn_up(B_HOME);
    settle();
    bad += check("HOME held until its list shows, then EDIT: the plain saw, the MENU stays shut, still on the designer's page",
                 ok && !ui.menu && !ui.home && !ui.shift && trk[0].preset == 0u && trk[0].p[P_E1] == 127 &&
                 GR_DESIGN(cur_graph()));
    press(B_HOME);
    frame();
    btn_down(B_HOME);
    frame();
    press(B_EDIT);
    btn_up(B_HOME);
    settle();
    bad += check("SHIFT + EDIT on HOME: nothing", ui.home && trk[0].preset == 0u);
    return bad;
}
/* FX's list: SELECT moves down its rows before the pages beside it; a row's four knobs are that effect's own */
static int test_fxlist(void)
{
    int bad = 0;
    uint32_t ok, i;
    int16_t d0, it0, sl0, dt0, dk0, bpm0;
    new_layout();
    bpm0 = song.g[G_BPM];
    press(B_FX);
    settle();
    ok = cur_graph() == GR_FX && sheet_of(GR_FX) && fxl_win == 0u;
    d0 = trk[0].p[P_DIST];
    turn(EN_K1, 5);                                  /* its own row: DIST, CHORUS, DELAY, REVERB (the sends) */
    turn(EN_K2, 5);
    ok &= trk[0].p[P_DIST] > d0 && trk[0].p[P_CHOR] > 0;
    bad += check("FX: the list first; on its own row the four knobs are the track's sends, as ever", ok);
    it0 = trk[0].p[P_ITYPE];
    dt0 = song.g[G_DTIME];
    sl0 = trk[0].p[P_SLCR];
    dk0 = trk[0].p[P_DKAMT];
    turn(EN_SELECT, 1);                              /* DRIVE */
    ok = cur_graph() == GR_FX && fxl_win == 1u;
    d0 = trk[0].p[P_DTONE];
    turn(EN_K3, 6);
    ok &= trk[0].p[P_DTONE] != d0;
    turn(EN_SELECT, 1);                              /* CHORUS: the send, then the song's chorus */
    d0 = trk[0].p[P_CHOR];
    turn(EN_K1, 7);
    ok &= fxl_win == 2u && trk[0].p[P_CHOR] > d0;
    d0 = song.g[G_CDEPTH];
    turn(EN_K4, -9);
    ok &= song.g[G_CDEPTH] < d0;
    turn(EN_SELECT, 1);                              /* DELAY: its TIME */
    turn(EN_K2, 2);
    ok &= fxl_win == 3u && song.g[G_DTIME] != dt0;
    turn(EN_SELECT, 2);                              /* INSERT: its kind */
    turn(EN_K1, 2);
    ok &= fxl_win == 5u && trk[0].p[P_ITYPE] != it0;
    turn(EN_SELECT, 1);                              /* SLICER */
    turn(EN_K1, 20);
    ok &= fxl_win == 6u && trk[0].p[P_SLCR] != sl0;
    turn(EN_SELECT, 1);                              /* DUCK */
    turn(EN_K2, 20);
    ok &= fxl_win == 7u && trk[0].p[P_DKAMT] != dk0 && cur_graph() == GR_FX;
    bad += check("SELECT: down the list, a row at a time; each row's knobs are that effect's own (DRIVE's TONE, CHORUS's "
                 "send and DEPTH, DELAY's TIME, INSERT's kind, SLICER, DUCK)", ok);
    turn(EN_SELECT, 1);
    ok = cur_graph() == GR_FX && fxl_win == 7u && song.g[G_BPM] == bpm0;
    for (i = 0; i < 9u; i++)
        turn(EN_SELECT, -1);
    ok &= cur_graph() == GR_FX && fxl_win == 0u && song.g[G_BPM] == bpm0;
    bad += check("... past its last row and its first: it stays there (SELECT is the tempo on the pages beside it), the tempo as it was", ok);
    ui_prefs |= PREF_BPM_LOCK;                       /* MENU > BPM LOCK ON: SELECT the pages; past the last row, the next */
    turn(EN_SELECT, 8);
    ok = cur_graph() == GR_FXDLY;
    turn(EN_SELECT, -1);
    ok &= cur_graph() == GR_FX && fxl_win == 7u;
    ui_prefs &= (uint8_t)~PREF_BPM_LOCK;
    for (i = 0; i < 7u; i++)
        turn(EN_SELECT, -1);
    bad += check("BPM LOCK ON: past the last row the next page (DELAY's own), and back onto the last row", ok && fxl_win == 0u);
    turn(EN_SELECT, 4);
    press(B_FX);
    settle();
    ok = cur_graph() == GR_FXDLY;
    bad += check("FX again from a row: the next page", ok);
    return bad;
}
/* (OP DREAM 1.2) MENU > THEME on the new layout: CRYPT and NEON are not offered, LAGOON and PEBBLE are; one of the two
 * kept from before is Skiff's default */
static int test_themes(void)
{
    int bad = 0;
    uint32_t ok = 1, i, n, seen = 0;
    new_layout();
    n = menu_n(MI_COLOR);
    for (i = 0; i < n; i++) {
        const char *nm = menu_vname(MI_COLOR, i);
        ok &= strcmp(nm, "CRYPT") && strcmp(nm, "NEON");
        seen |= (uint32_t)!strcmp(nm, "LAGOON") | (uint32_t)!strcmp(nm, "PEBBLE") << 1 | (uint32_t)!strcmp(nm, "QUARK") << 2 |
                (uint32_t)!strcmp(nm, "SKIFF") << 3;
        menu_put(MI_COLOR, i);
        ok &= menu_get(MI_COLOR) == i && !strcmp(UI_PALETTES[settings.palette].name, nm) && !ux.crypt && !ux.neon;
    }
    bad += check("THEME: nine to pick (SKIFF HI-CON PAPER QUARK TAPE PEA CANDY LAGOON PEBBLE), no CRYPT, no NEON; each one "
                 "picked is the one shown", ok && n == 9u && seen == 15u);
    settings.palette = UI_CRYPT_INDEX;               /* (kept from Skiff) */
    theme_apply();
    ok = settings.palette == UI_SKIFF_INDEX && !ux.crypt;
    settings.palette = UI_NEON_INDEX;
    theme_apply();
    bad += check("CRYPT or NEON stored from before: SKIFF", ok && settings.palette == UI_SKIFF_INDEX && !ux.neon);
    menu_put(MI_COLOR, 0);
    return bad;
}
static int test_keep2(void)
{
    int bad = 0;
    int16_t d[P_COUNT];
    uint32_t ok;
    new_layout();
    snap();
    hold_btn(B_EDIT, 1);
    bad += check("EDIT held before anything was designed: said, nothing changed", msg_is("NOTHING DESIGNED YET") && same_as_snap(0) &&
                 ui.home);
    press(B_EDIT);
    settle();
    turn(EN_K1, 4);
    turn(EN_K4, 9);
    memcpy(d, trk[0].p, sizeof d);
    hold_btn(B_EDIT, 1);
    bad += check("EDIT held on the designer's page: track 1 has the sound, said so, still on the page",
                 !design_trk && trk[0].eng_req == ENGI_WAVE && !memcmp(d, trk[0].p, sizeof d) && cur_graph() == GR_DSND &&
                 msg_is("T1 HAS THE NEW SOUND") && !strcmp(ui.msg2, "HOLD [SAVE]: UNDO") && undo_n == 1u);
    press(B_HOME);
    settle();
    bad += check("HOME: it stays", trk[0].eng_req == ENGI_WAVE && trk[0].engine == ENGI_WAVE && !memcmp(d, trk[0].p, sizeof d));
    hold_btn(B_SAVE, 1);
    settle();
    bad += check("SAVE held: the track's own sound back, every value", same_as_snap(0) && trk[0].engine == snap_e[0]);
    press(B_OCTUP);                                  /* (the undo window: OCT+ redoes) */
    settle();
    bad += check("OCT+ (redo): the designer's sound again", trk[0].eng_req == ENGI_WAVE && !memcmp(d, trk[0].p, sizeof d));
    frames(4000);
    hold_btn(B_EDIT, 1);
    bad += check("EDIT held on that track again: it has it already", msg_is("T1 ALREADY HAS IT") && !memcmp(d, trk[0].p, sizeof d));
    sel_set(2);
    frame();
    hold_btn(B_EDIT, 1);
    settle();
    {
        uint32_t i;
        for (i = 0, ok = trk[2].eng_req == ENGI_WAVE; i < P_COUNT; i++)
            if (!param_kept(i))
                ok &= trk[2].p[i] == trk[0].p[i];
            else
                ok &= trk[2].p[i] == snap_p[2][i];   /* (its mix, its ARP, its pattern's values: the track's own) */
    }
    bad += check("EDIT held on track 3, from HOME: the same sound written there, its mix its own", ok && ui.home &&
                 msg_is("T3 HAS THE NEW SOUND"));
    hold_btn(B_SAVE, 1);
    settle();
    bad += check("SAVE held: track 3's own sound back", same_as_snap(2) && trk[0].eng_req == ENGI_WAVE);
    sel_set(4);
    frame();
    hold_btn(B_EDIT, 1);
    ok = msg_is("DESIGNER: TRACKS 1-4") && same_as_snap(4);
    press(B_EDIT);
    ok &= msg_is("DESIGNER: TRACKS 1-4") && same_as_snap(4) && ui.home && !design_trk;
    sel_set(5);
    frame();
    press(B_EDIT);
    ok &= same_as_snap(5) && ui.home && !design_trk;
    bad += check("tracks 5 and 6: EDIT and EDIT held say why not, nothing changes", ok);
    return bad;
}

static int test_own2(void)
{
    int bad = 0;
    int16_t d[P_COUNT];
    uint32_t i, ok;
    new_layout();
    sel_set(1);
    set_engine_of(&trk[1], ENGI_WAVE);
    apply_preset_to(&trk[1], 4);
    undo_clear();
    frame();
    snap();
    press(B_EDIT);
    settle();
    bad += check("EDIT on a WAVE track: its own sound on SOUND, nothing tried", !design_trk && cur_graph() == GR_DSND && same_as_snap(1));
    turn(EN_K1, 2);
    turn(EN_K2, 11);
    memcpy(d, trk[1].p, sizeof d);
    bad += check("the knobs change the track's sound itself", trk[1].p[P_E0] != snap_p[1][P_E0] || trk[1].p[P_E1] != snap_p[1][P_E1]);
    press(B_HOME);
    settle();
    bad += check("HOME: it keeps them", !memcmp(d, trk[1].p, sizeof d));
    sel_set(0);
    frame();
    press(B_EDIT);
    settle();
    for (i = 0, ok = design_trk == 1u && trk[0].eng_req == ENGI_WAVE; i < P_COUNT; i++)
        if (!param_kept(i))
            ok &= trk[0].p[i] == d[i];
    bad += check("EDIT on track 1 then: that sound, as it was left, tried there", ok && same_as_snap(2));
    press(B_HOME);
    settle();
    bad += check("HOME: track 1's own back, track 2 keeps its sound", same_as_snap(0) && !memcmp(d, trk[1].p, sizeof d));
    return bad;
}

static int test_undo_order(void)
{
    static const pn_t P[] = {{0, 20, 60}, {30, 20, 64}};
    int bad = 0;
    uint8_t fm[FP_SIZE];
    uint32_t n0, ok, mc;
    new_layout();
    take(0, P, 2, 80, 1);                            /* a take on track 1's tape */
    frame();
    n0 = tape_count(0);
    snap();
    press(B_EDIT);
    settle();
    turn(EN_PRESET, 1);
    turn(EN_PRESET, 1);
    turn(EN_PRESET, 1);
    ok = trk[0].preset == 3u && trk[0].eng_req == ENGI_WAVE && cur_graph() == GR_DSND && design_trk == 1u;
    turn(EN_PRESET, -9);
    ok &= trk[0].preset == 0u;
    turn(EN_PRESET, 40);
    ok &= trk[0].preset == ENG_WAVE.npresets - 1u && trk[0].eng_req == ENGI_WAVE;
    bad += check("PRESETS on the designer's page: WAVE's sounds one by one, its ends where they are", ok);
    press(B_HOME);
    settle();
    bad += check("HOME: the track's own sound back; what the trial loaded leaves nothing to undo", same_as_snap(0) && !undo_n);
    hold_btn(B_SAVE, 1);
    settle();
    bad += check("SAVE held then: the take made before the trial is what goes (TAKE UNDONE)", n0 == 2u && !tape_count(0) &&
                 msg_is("TAKE UNDONE") && same_as_snap(0));
    {   /* an FM6 track's patch, a track's automation */
        new_layout();
        sel_set(1);
        set_engine_of(&trk[1], ENGI_FM6);
        apply_preset_to(&trk[1], 3);
        (void)motion_set_lock(&trk[1], 2, P_REL, 50);
        (void)motion_set_lock(&trk[1], 5, P_ATK, 20);
        motion_set_enabled(&trk[1], 1);
        undo_clear();
        frame();
        memcpy(fm, fm6_patch[1], FP_SIZE);
        mc = motion_count(&trk[1]);
        snap();
        press(B_EDIT);
        settle();
        ok = design_trk == 2u && trk[1].eng_req == ENGI_WAVE && !motion_count(&trk[1]);
        turn(EN_K1, 2);
        press(B_HOME);
        settle();
        bad += check("an FM6 track with automation: tried without it, then its sound, its patch and its automation back whole",
                     ok && mc == 2u && same_as_snap(1) && !memcmp(fm, fm6_patch[1], FP_SIZE) && motion_count(&trk[1]) == mc &&
                     motion_find(&trk[1], 5, P_ATK) >= 0);
        press(B_EDIT);
        settle();
        hold_btn(B_EDIT, 1);
        ok = trk[1].eng_req == ENGI_WAVE && !design_trk;
        hold_btn(B_SAVE, 1);
        settle();
        bad += check("kept, then SAVE held: the FM6 sound, its patch and its automation back", ok && same_as_snap(1) &&
                     !memcmp(fm, fm6_patch[1], FP_SIZE) && motion_count(&trk[1]) == mc);
    }
    return bad;
}

static uint32_t fam_count(uint32_t fam, uint32_t *has, uint32_t g, uint32_t id0)   /* the pages a button shows; *has: one
                                                      * of graph g (and first id id0, when not 0xFF) among them */
{
    uint32_t i, n = 0;
    *has = 0;
    for (i = 0; i < NPAGES; i++)
        if (page_fam(&PAGES[i]) == fam && page_visible(i)) {
            n++;
            if (PAGES[i].graph == g && (id0 == 0xFFu || PAGES[i].id[0] == id0))
                *has = 1;
        }
    return n;
}
static int test_pages2(void)
{
    int bad = 0;
    uint32_t has, has2, n, ok;
    new_layout();
    n = fam_count(FAM_EDIT, &has, GR_DOSCA, 0xFFu);
    bad += check("an ANALOG track: EDIT has no page of its own left (the designer's come with its sound)", !n && !has);
    n = fam_count(FAM_FX, &has, GR_NONE, P_E0);
    (void)fam_count(FAM_FX, &has2, GR_VOICE, 0xFFu);
    ok = has && has2 && n == 7u + 3u;                 /* FX DELAY REVERB DRIVE INSERT SLICER DUCK + OSC, VOICE, VOICE 2 */
    (void)fam_count(FAM_ENV, &has, GR_NONE, P_E4);
    n = fam_count(FAM_ENV, &has2, GR_ADSR, P_ATK);
    bad += check("its OSC and VOICE pages under FX, after FX's own; its FLT under ENV, after ENV 2", ok && has && has2 && n == 3u);
    press(B_FX);
    ok = cur_graph() == GR_FX;
    press(B_ENV);
    ok &= cur_graph() == GR_ADSR && cur_page()->id[0] == P_ATK;
    press(B_ENV);
    press(B_ENV);
    ok &= cur_page()->scope == SC_ENGINE && cur_page()->id[0] == P_E4;
    press(B_ENV);
    ok &= cur_graph() == GR_ADSR && cur_page()->id[0] == P_ATK;
    bad += check("FX opens on FX, ENV on ENV; ENV again and again: ENV 2, FLT, ENV", ok);
    sel_set(1);
    set_engine_of(&trk[1], ENGI_FM6);
    frame();
    (void)fam_count(FAM_FX, &has, GR_FMOP, 0xFFu);
    (void)fam_count(FAM_FX, &has2, GR_FMALL, 0xFFu);
    n = fam_count(FAM_EDIT, &ok, GR_FMOP, 0xFFu);
    bad += check("an FM6 track: OPERATOR and ALL OPS under FX, none under EDIT", has && has2 && !n);
    sel_set(2);
    set_engine_of(&trk[2], ENGI_WAVE);
    frame();
    n = fam_count(FAM_EDIT, &has, GR_DFLT, 0xFFu);
    (void)fam_count(FAM_FX, &has2, GR_NONE, P_E0);
    ok = n == 5u && has && !has2;
    n = fam_count(FAM_ENV, &has, GR_DFLT, 0xFFu);
    (void)fam_count(FAM_ENV, &has2, GR_NONE, P_E4);
    bad += check("a WAVE track: the designer's five pages under EDIT (FILTER the last), ENV and ENV 2 under ENV, its own two pages not shown",
                 ok && n == 2u && !has && !has2);
    {   /* NOTES, armed: EDIT clears the cursor's step, as ever; no trial */
        new_layout();
        press(B_SEQ);
        trk[0].step[0].time = ST_NOTE;
        trk[0].step[0].n = 1;
        trk[0].step[0].note[0] = 60;
        trk[0].step[0].vel = 100;
        song.rec = 1;
        ui.cursor = 0;
        frame();
        snap();
        press(B_EDIT);
        bad += check("on NOTES with the track armed, EDIT still clears the step: no designer, the sound as it was",
                     step_page(cur_graph()) && trk[0].step[0].time != ST_NOTE && !design_trk && same_as_snap(0));
        song.rec = 0;
    }
    {   /* the layer EDIT had (the engines' grid) is not there: EDIT held with SHIFT does not lock one */
        new_layout();
        shift_lock(B_EDIT);
        bad += check("SHIFT + EDIT: no layer locked, no trial", !ui.lock && !ui.ly && !design_trk);
    }
    return bad;
}

static int test_save2(void)
{
    static const pn_t P[] = {{0, 20, 60}, {30, 20, 64}, {70, 10, 67}};
    int bad = 0;
    uint32_t ok, has, n, k;
    char nm[16];
    new_layout();
    press(B_SAVE);
    ok = cur_graph() == GR_PSAVE;
    press(B_SAVE);
    ok &= cur_graph() == GR_SSAVE;
    press(B_SAVE);
    ok &= cur_graph() == GR_BROWSE;
    press(B_SAVE);
    ok &= cur_graph() == GR_PSAVE;
    n = fam_count(FAM_SAVE, &has, GR_USER, 0xFFu);
    ok &= n == 3u && !has;
    (void)fam_count(FAM_SAVE, &has, GR_SLOTS, 0xFFu);
    ok &= !has;
    (void)fam_count(FAM_SAVE, &has, GR_BANK, 0xFFu);
    ok &= !has;
    (void)fam_count(FAM_SEQ, &has, GR_BANK, 0xFFu);
    bad += check("SAVE: PROJECT SAVE, SOUND SAVE, the list, round again; PATTERNS is under SEQ", ok && has);
    press(B_HOME);
    take(0, P, 3, 100, 1);
    frame();
    trk[2].p[P_E4] = 33;
    press(B_SAVE);
    turn(EN_K1, 2);                                  /* slot C */
    ok = psave_row == 2u;
    press(B_OCTUP);                                  /* the NAME screen, then OCT+: saved */
    ok &= name_on();
    press(B_OCTUP);
    frame();
    bad += check("PROJECT SAVE: KNOB 1 the slot, OCT+ the name, OCT+ again: the whole project kept in it; the octave where it was",
                 ok && !name_on() && project_used(2) && !project_used(0) && tape_count(0) == 3u && !song.octave);
    press(B_HOME);
    hold_btn(B_REC, 1);                              /* (the track's tape wiped, a take armed) */
    settle();
    press(B_REC);                                    /* (disarmed) */
    settle();
    trk[2].p[P_E4] = 90;
    undo_clear();
    tape.back = TU_NONE;
    ok = !tape_count(0);
    press(B_SAVE);
    ok &= cur_graph() == GR_PSAVE;
    turn(EN_K1, -3);                                 /* slot A: empty */
    press(B_OCTDN);
    ok &= msg_is("EMPTY SLOT");
    turn(EN_K1, 2);
    press(B_OCTDN);                                  /* over changed music: asked first */
    if (ui.confirm)
        press(B_OCTUP);
    settle();
    bad += check("OCT- on an empty slot: said; on C: the project back, its tape with it", ok && tape_count(0) == 3u &&
                 trk[2].p[P_E4] == 33);
    {   /* NEW: the row after D */
        turn(EN_K1, 9);
        ok = psave_row == 4u;
        press(B_OCTUP);
        ok &= msg_is("PICK A SLOT: A TO D") && tape_count(0) == 3u;
        press(B_OCTDN);                              /* (nothing changed since the load: no question) */
        settle();
        ok &= !ui.confirm && !tape_count(0) && !tape.len && msg_is("NEW PROJECT") && project_used(2);
        press(B_HOME);
        take(1, P, 3, 100, 1);                       /* something played: asked first */
        frame();
        press(B_SAVE);
        turn(EN_K1, 9);
        press(B_OCTDN);
        ok &= ui.confirm && tape_count(1) == 3u;
        press(B_OCTUP);
        settle();
        bad += check("NEW, the row after D: OCT+ says to pick a slot; OCT- an empty project (asked first over changed music)",
                     ok && !ui.confirm && !tape_count(1));
    }
    /* SOUND SAVE */
    new_layout();
    set_engine_of(&trk[2], ENGI_WAVE);
    apply_preset_to(&trk[2], 5);
    trk[2].p[P_E1] = 17;
    trk[2].p[WX(WX_ATHICK)] = 77;                    /* (one of WAVE's sixteen more: kept with the sound as the eight are) */
    trk[2].p[WX(WX_FLT)] = (int16_t)WVF(WF_BAND, WR_A | WR_SUB);
    undo_clear();
    snap();
    press(B_SAVE);
    press(B_SAVE);
    ok = cur_graph() == GR_SSAVE && ui.uslot == 0u;
    turn(EN_K1, 2);                                  /* track 3 */
    ok &= song.sel == 2u;
    turn(EN_K2, 3);                                  /* slot U04 */
    ok &= ui.uslot == 3u;
    press(B_OCTUP);
    ok &= name_on();
    press(B_OCTUP);
    frame();
    up_name(3, nm);
    bad += check("SOUND SAVE: KNOB 1 the track, KNOB 2 the slot, OCT+ the name, OCT+ again: that track's sound in the slot",
                 ok && !name_on() && up_used(3) && !up_used(0) && up_rec(3)->engine == ENGI_WAVE && nm[0] && same_as_snap(2) == 0 &&
                 trk[2].user == 4u);
    press(B_HOME);
    press(B_SAVE);
    if (cur_graph() != GR_SSAVE)
        press(B_SAVE);
    ok = cur_graph() == GR_SSAVE && ui.uslot == 0u;  /* (opens on the first empty slot) */
    press(B_OCTDN);
    ok &= msg_is("EMPTY SLOT");
    turn(EN_K1, -1);                                 /* track 2 */
    turn(EN_K2, 3);
    press(B_OCTDN);
    settle();
    for (k = 0, has = trk[1].eng_req == ENGI_WAVE; k < P_COUNT; k++)
        if (!param_kept(k))
            has &= trk[1].p[k] == trk[2].p[k];
    bad += check("it opens on the first empty slot (OCT-: EMPTY SLOT); OCT- on U04 with track 2 picked: the sound on track 2",
                 ok && song.sel == 1u && has && trk[1].p[P_E1] == 17 && !song.octave && trk[1].p[WX(WX_ATHICK)] == 77 &&
                 wv_fkind(trk[1].p) == WF_BAND && wv_through(trk[1].p) == (WR_A | WR_SUB));
    hold_btn(B_SAVE, 1);
    settle();
    bad += check("SAVE held: track 2's own sound back", same_as_snap(1));
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += test_trial2();
    bad += test_fresh();
    bad += test_fxlist();
    bad += test_themes();
    bad += test_keep2();
    bad += test_own2();
    bad += test_undo_order();
    bad += test_pages2();
    bad += test_save2();
    printf(bad ? "newui: %d FAILED\n" : "newui: all ok\n", bad);
    return bad != 0;
}
