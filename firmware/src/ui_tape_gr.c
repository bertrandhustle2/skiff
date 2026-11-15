/* SPDX-License-Identifier: GPL-3.0-only
 * The tape's pictures (tape.c, ui_tape.c): SEQ > TAPE's panel (two reels, the time between them, the six tracks'
 * lanes under them), HOME's lanes with the tape in them (ui_home.c graph_tracks), HOME's bottom line (ui_draw.c).
 * Included by ui_graph.c (after ui_quark.c: qk_disc, QK_COS; before ui_home.c). */
#define TR_R 16                                          /* a reel: its radius, where it is (its rim over the lanes' ends) */
#define TR_LX (TP_X0 + TR_R)
#define TR_RX (TP_X0 + TP_W - 1 - TR_R)
#define TR_CY 19
#define TR_TX (TR_LX + TR_R + 6)                         /* the words between them */
#define TR_TW (TR_RX - TR_R - 6 - TR_TX)

/* a reel: its rim, the tape wound on it (radius rp), three spokes turned by rot (a turn 2^32), the hub */
static void tape_reel(int32_t cx, int32_t rp, uint32_t rot, uint16_t hub)
{
    uint32_t k;
    qk_disc(cx, TR_CY, TR_R, T_MID);
    qk_disc(cx, TR_CY, TR_R - 1, T_BG);
    qk_disc(cx, TR_CY, rp, ux_mix(T_BG, T_TEXT, 45));
    for (k = 0; k < 3u; k++) {
        uint32_t ph = rot + k * 0x55555555u;
        cv_line(cx, TR_CY, cx + (TR_R - 2) * QK_COS(ph) / 32767, TR_CY + (TR_R - 2) * QK_SIN(ph) / 32767, T_BG);
    }
    qk_disc(cx, TR_CY, 4, hub);
    cv_rect(cx - 1, TR_CY - 1, 2, 2, T_BG);
}

/* a note's bar in a strip from y, ch high: where it starts (span over w px from x0), as long as it was held, low
 * notes low */
static void tape_bar(const tape_note_t *e, uint32_t span, int32_t x0, int32_t w, int32_t y, int32_t ch, uint16_t c)
{
    uint32_t st = tape_st_of(e), ln, nt;
    int32_t a, b;
    ln = e->len ? (uint32_t)e->len * TAPE_LEN_U : tape.pos > st ? tape.pos - st : TAPE_LEN_U;   /* (held: to now) */
    nt = e->note < 24u ? 0u : e->note > 96u ? 72u : e->note - 24u;
    a = tape_xw(st, span, x0, w);
    b = tape_xw(st + ln, span, x0, w);
    cv_rect(a, y + 1 + (int32_t)((72u - nt) * (uint32_t)(ch - 4) / 72u), b > a ? b - a : 1, 2, c);
}

static void sound_name(const track_t *t, char *b);       /* (ui_draw.c) */
#define TH_X0 86                                         /* HOME (the new layout): a lane's tape, right of its sound's name */
#define TH_W 144
/* the tape's panel: the reels, the time and what to do between them, the six lanes. home: HOME's (each lane with its
 * track's sound, the selected one a band of its colour); else SEQ > TAPE's of Skiff's layout */
static void tape_panel(int home)
{
    uint32_t span = tape_view(), k, i, n = tape.n, live = 0, rot = tape_turn() << 28;
    int32_t x0 = home ? TH_X0 : TP_X0, w = home ? TH_W : TP_W, px, bot = TP_Y0 + TP_PITCH * (int32_t)NTRK - (TP_PITCH - TP_LH), wound;
    int rec = tape_rec_on();
    uint16_t nc[NTRK];
    const char *say;
    char b[28];
    wound = (int32_t)(((tape.pos > span ? span : tape.pos) >> 8) * 9u / ((span >> 8) ? (span >> 8) : 1u));
    cv_rect(TR_LX, TR_CY + TR_R + 1, TR_RX - TR_LX + 1, 1, T_MID);   /* the tape, reel to reel, the head on it */
    tape_reel(TR_LX, TR_R - 1 - wound, rot, rec ? T_REC : T_TEXT);
    tape_reel(TR_RX, TR_R - 10 + wound, rot, rec ? T_REC : T_TEXT);
    cv_rect(TR_TX + TR_TW / 2 - 4, TR_CY + TR_R, 9, 3, rec ? T_REC : tape.run ? T_ACCENT : T_MID);   /* (the head) */
    tape_time(b, tape.pos, !tape.run);                   /* the time, large (red: recording); LOOP, how full the tape is */
    cv_text_on(TR_TX, 2, &AF_M, b, rec ? T_REC : tape.run ? T_ACCENT : T_TEXT, T_SURF);
    for (i = 0; i < n; i++)
        live += (uint32_t)tape_live(&tape_nt[i]);
    fmt_int(b, (int32_t)live);
    str_cpy(b + str_len(b), "/", sizeof b - str_len(b));
    fmt_int(b + str_len(b), (int32_t)TAPE_NOTES);
    if (home && tape.loop && !tape.full)
        cv_text_r(TR_TX + TR_TW, 5, &AF_S, "LOOP", T_ACCENT, T_SURF);
    else if (!home || tape.full || live * 4u >= TAPE_NOTES * 3u)
        cv_text_r(TR_TX + TR_TW, 5, &AF_S, b, tape.full ? T_REC : T_MID, T_SURF);
    say = tape.full ? (tape.full == TF_NOTES ? "TAPE FULL" : tape.full == TF_RUNS ? "STOP TO GO ON" : "TAPE AT ITS END")
          : rec && !tape.run ? "PLAY A KEY TO START" : rec ? "REC ENDS THE TAKE" : live ? "REC ADDS A LAYER"
          : "REC, THEN PLAY KEYS";
    cv_text_on(TR_TX, 18, &AF_S, say, tape.full ? T_REC : T_MID, T_SURF);
    for (k = 0; k < NTRK; k++) {                         /* the lanes */
        int32_t y = TP_Y0 + TP_PITCH * (int32_t)k, ib[4];
        int sel = k == song.sel;
        char d[2] = {(char)('1' + k), 0};
        text_ink(&AF_S, d, ib);
        if (home) {                                      /* its number, its sound, its tape; the selected one a band */
            const track_t *t = &trk[k];
            int muted = t->p[P_MUTE] != 0;
            uint16_t col = muted ? T_DIM : T_TRK(k), band = sel ? col : T_SURF;
            char name[20];
            if (sel)
                cv_rect(3, y - 1, 234, TP_LH + 2, col);
            GFX_HOOK_ALIGN(0, y, 0, y + TP_LH, AL_V, "home tape lane number on its middle");
            cv_text_on(9 - ib[0], y + CAP_IN(S, TP_LH), &AF_S, d, sel ? T_INK : col, band);
            sound_name(t, name);
            GFX_HOOK_ALIGN(0, y, 0, y + TP_LH, AL_V, "home tape lane name on its middle");
            cv_free_text(20, y + CAP_IN(S, TP_LH), &AF_S, name[0] ? name : "--", sel ? T_INK : muted ? T_DIM : T_TEXT, band,
                         x0 - 4 - 20);
            cv_rect(x0, y, w, TP_LH, sel ? ux_mix(col, T_INK, 55) : T_BG);
            nc[k] = sel ? T_INK : col;
        } else {                                         /* (the selected track's raised) */
            cv_rect(x0, y, w, TP_LH, sel ? T_RAISE : T_BG);
            GFX_HOOK_ALIGN(0, y, 0, y + TP_LH, AL_V, "tape lane number on its middle");
            cv_text_on(11 - ib[0], y + CAP_IN(S, TP_LH), &AF_S, d, sel ? T_TEXT : T_TRK(k), T_SURF);
            nc[k] = T_TRK(k);
        }
    }
    for (i = 0; i < n; i++) {                            /* the notes */
        const tape_note_t *e = &tape_nt[i];
        if (!tape_live(e))
            continue;
        k = tape_trk_of(e) % NTRK;
        tape_bar(e, span, x0, w, TP_Y0 + TP_PITCH * (int32_t)k, TP_LH, nc[k]);
    }
    if (tape.len && tape.len < span)                     /* the end, when the tape rolled on past it */
        cv_rect(tape_xw(tape.len, span, x0, w), TP_Y0 - 2, 1, bot - TP_Y0 + 4, T_MID);
    px = tape_xw(tape.pos, span, x0, w);                 /* the playhead (2 px: it stays there on a screen shown smaller) */
    cv_rect(px, TP_Y0 - 2, 2, bot - TP_Y0 + 4, rec ? T_REC : T_ACCENT);
    if (home)                                            /* (.. ink where it crosses the selected lane's band) */
        cv_rect(px, TP_Y0 + TP_PITCH * (int32_t)(song.sel % NTRK) - 1, 2, TP_LH + 2, T_INK);
}
static void graph_tape(void) { tape_panel(0); }
static void graph_tape_home(void) { tape_panel(1); }
/* what HOME shows in the new layout (graph_signature): the sounds, the selection, the tape */
static uint32_t tape_newhome_sig(void)
{
    uint32_t h = 2166136261u ^ song.sel * 7919u ^ ux.gen * 131u, k;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        h = (h ^ (7u * (uint32_t)t->p[P_MUTE] + 31u * t->eng_req + 257u * t->preset + 65537u * t->user + up_gen * 977u)) * 16777619u;
    }
    return h ^ tape_sig();
}

/* HOME: the lanes that show their tape (graph_tracks notes them here as it draws the rows, then draws the tape once) */
static struct {
    int16_t y, h, cy, ch;                                /* the row, the strip in it; ch 0: not this track's */
    uint16_t on;
    uint8_t sel;
} tape_hl[NTRK] __attribute__((section(".pool")));

static void tape_home_strip(uint32_t k, int32_t y, int32_t h, int32_t ch, uint16_t on, uint16_t off, int sel)
{
    k %= NTRK;
    tape_hl[k].y = (int16_t)y;
    tape_hl[k].h = (int16_t)h;
    tape_hl[k].cy = (int16_t)(y + (h - ch) / 2);
    tape_hl[k].ch = (int16_t)ch;
    tape_hl[k].on = on;
    tape_hl[k].sel = (uint8_t)sel;
    cv_rect(TP_HX0, tape_hl[k].cy, TP_HW, ch, off);
}
static void tape_home_notes(void)
{
    uint32_t span = tape_view(), i, n = tape.n, k;
    int32_t px = tape_xw(tape.pos, span, TP_HX0, TP_HW), ex = tape_xw(tape.len, span, TP_HX0, TP_HW);
    int rec = tape_rec_on();
    for (i = 0; i < n; i++) {
        const tape_note_t *e = &tape_nt[i];
        if (!tape_live(e))
            continue;
        k = tape_trk_of(e) % NTRK;
        if (tape_hl[k].ch)
            tape_bar(e, span, TP_HX0, TP_HW, tape_hl[k].cy, tape_hl[k].ch, tape_hl[k].on);
    }
    for (k = 0; k < NTRK; k++) {
        if (!tape_hl[k].ch)
            continue;
        if (tape.len && tape.len < span)                 /* the end, when the tape rolled on past it */
            cv_rect(ex, tape_hl[k].cy, 1, tape_hl[k].ch, tape_hl[k].on);
        if (tape.run || rec)                             /* the playhead, as the steps' */
            cv_rect(px, tape_hl[k].y + 2, 2, tape_hl[k].h - 4, rec && !tape_hl[k].sel ? T_REC : tape_hl[k].sel ? T_INK : T_TEXT);
    }
}
