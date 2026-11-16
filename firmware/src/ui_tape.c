/* SPDX-License-Identifier: GPL-3.0-only
 * The tape on the screens (tape.c). The new layout (UI_NEW, the unit's): HOME is the tape's screen (two reels, the
 * six tracks' sounds, each with its lane of the tape, every note where it was played and as long as it was held, the
 * playhead, the end); REC records onto it on every page but SEQ's, where it records the steps as Skiff does. REC
 * held: the selected track from scratch (its steps cleared and its tape wiped; a take armed, but on SEQ's pages). PLAY held, or
 * SHIFT + PLAY: LOOP. SAVE held undoes the tape's last take or wipe when that is the newest thing to undo.
 * Skiff's own layout (the host tests'): the tape's page is SEQ > TAPE, KNOB 1 LOOP, KNOB 2 END, REC there records
 * onto it, SHIFT + EDIT wipes the selected track's, SHIFT + OCT- takes the last take or wipe back.
 * Included by ui.c (before ui_graph.c: draw_graph, graph_signature); the pictures: ui_tape_gr.c (ui_graph.c). */
#define TP_X0 24                                         /* the lanes: x 24 .. 229 */
#define TP_W 206
#define TP_Y0 42                                         /* the first lane's top (the reels over them); 15 apart, 13 high */
#define TP_PITCH 15
#define TP_LH 13
#define TP_SEC 44100u
#define TP_HX0 100                                       /* HOME: a lane's tape, where its 16 cells were (ui_home.c) */
#define TP_HW 134

static int tape_page(void) { return OLD_UI(!ui.home && cur_page()->graph == GR_TAPE); }
/* power-on (main.c): the new layout */
static void tape_power_on(void) { tape.to_pat = BOARD_SMK37 ? 1 : 0; }   /* (the SMK-37: Skiff's own layout) */

/* the stretch of time the lanes show: the tape to its end; with no end yet, what was played so far, 10 s at least, 5 s
 * at a time (the picture rescales in steps, not with every block) */
static uint32_t tape_view(void)
{
    uint32_t span = tape.len, p = tape.pos;
    if (span && (p <= span || !tape.run))
        return span;
    if (p > span)
        span = p;
    span = (span / (5u * TP_SEC) + 1u) * 5u * TP_SEC;
    return span < 10u * TP_SEC ? 10u * TP_SEC : span;
}
static int32_t tape_xw(uint32_t s, uint32_t span, int32_t x0, int32_t w)   /* (no 64-bit divide on the chip: both scaled down) */
{
    uint32_t d = span >> 8;
    if (s > span)
        s = span;
    return x0 + (int32_t)(((s >> 8) * (uint32_t)(w - 1)) / (d ? d : 1u));
}
static int32_t tape_x(uint32_t s, uint32_t span) { return tape_xw(s, span, TP_X0, TP_W); }
/* samples as m:ss, with tenths when asked (b: 12 bytes) */
static void tape_time(char *b, uint32_t s, int tenths)
{
    uint32_t t = s / (TP_SEC / 10u), m = t / 600u, sec = t / 10u % 60u, n;
    fmt_int(b, (int32_t)m);
    n = str_len(b);
    b[n++] = ':';
    b[n++] = (char)('0' + sec / 10u);
    b[n++] = (char)('0' + sec % 10u);
    if (tenths) {
        b[n++] = '.';
        b[n++] = (char)('0' + t % 10u);
    }
    b[n] = 0;
}
/* the reels' turn as drawn: a turn every 2 s, in 16 places (the picture changes 8 times a second, no more) */
static uint32_t tape_turn(void) { return (tape.pos * 48696u) >> 28; }
/* what the page's picture shows (graph_signature): the notes, the playhead's pixel, the reels, the second, the states */
static uint32_t tape_sig(void)
{
    uint32_t span = tape_view();
    return (uint32_t)tape.gen * 2654435761u + (uint32_t)tape_x(tape.pos, span) * 7919u + tape.len * 31u +
           (uint32_t)tape_rec_on() * 3u + (uint32_t)tape.run * 5u + (uint32_t)tape.loop * 17u +
           (uint32_t)tape.full * 257u + (tape.pos / TP_SEC) * 65537u + (tape.run ? 0u : tape.pos) + span * 13u +
           tape_turn() * 0x9E3779u;
}

/* HOME (ui_home.c): does track k's lane show its tape (not its pattern's 16 cells)? With notes of its on the tape, or
 * the tape recording: yes. Else, REC TO TAPE: yes unless its pattern holds something; REC TO PATTERN: no */
static int tape_home_row(uint32_t k, const uint16_t *cnt)
{
    const step_t *s = trk[k % NTRK].step;
    uint32_t i;
    if (cnt[k % NTRK] || tape_rec_on())
        return 1;
    if (tape.to_pat)
        return 0;
    for (i = 0; i < NSTEP; i++)
        if (s[i].n || s[i].hit)
            return 0;
    return 1;
}
/* ... and what those lanes show (home_sig) */
static uint32_t tape_home_sig(void)
{
    uint32_t span = tape_view();
    return (uint32_t)tape.gen * 40503u + (uint32_t)tape_rec_on() * 977u + (uint32_t)tape.to_pat * 131u + span * 7u +
           (tape.run ? (uint32_t)tape_xw(tape.pos, span, TP_HX0, TP_HW) * 65599u : 0u) + (uint32_t)tape.run * 0x51EDu;
}
/* (SEQ > TAPE, Skiff's layout) KNOB 1: LOOP; KNOB 2: END */
static void tape_knob(uint32_t slot, int32_t steps)
{
    if (slot == 0u)
        tape.loop = (uint8_t)(steps > 0);
    else if (slot == 1u)
        tape_end_turn(steps);
}

/* REC on the tape (ui_input.c rec_tap: on its page, or while it records, from any page) */
static void tape_ui_rec(void)
{
    int was = tape_rec_on();
    song.rec = 0;                                        /* (the steps' recording and the tape's: one at a time) */
    tape_rec_toggle();
    ui.force = 1;
    if (!was)
        ui_message(song.playing ? "TAPE RECORDS" : "PLAY A KEY TO START");
}
/* SHIFT + PLAY (HOME with REC TO TAPE, the tape's page): LOOP on / off */
static int tape_loop_key(void) { return (ui.home && !tape.to_pat) || tape_page(); }
static void tape_ui_loop(void)
{
    tape.loop = (uint8_t)!tape.loop;
    ui.force = 1;
    ui_message(tape.loop ? "TAPE LOOP ON" : "TAPE LOOP OFF");
}
/* SHIFT + EDIT there: the selected track's notes off the tape */
static void tape_ui_clear(void)
{
    char b[4] = {'T', (char)('1' + song.sel % NTRK), 0, 0};
    if (!tape_count(song.sel % NTRK)) {
        ui_say(b, " HAS NO TAPE");
        return;
    }
    tape_clear(1u << (song.sel % NTRK));
    ui_say(b, " OFF THE TAPE");
}
/* SHIFT + OCT- there: the last take, or the last clear, taken back */
static void tape_ui_undo(void)
{
    if (tape.back == TU_NONE) {
        ui_message("NOTHING TO TAKE BACK");
        return;
    }
    ui_message(tape.back == TU_TAKE ? "TAKE UNDONE" : "WIPE UNDONE");
    tape_undo_ask();
}
/* which undo SAVE held does: the tape's when its last take or wipe came after Skiff's newest copy (ui.c undo_push) */
static uint16_t tape_useq __attribute__((section(".pool")));
static int tape_undo_newest(void) { return tape.back != TU_NONE && tape_useq == upin.seq && !undo_back; }

/* REC held (ui_input.c): the selected track from scratch, on every page: its steps go (Skiff's own clear, one undo)
 * and its tape is wiped (one undo of the tape's: SAVE held takes the wipe back, held again the steps). On SEQ's
 * pages that is all, at once (REC records steps there). Everywhere else the tape is stopped first and a take armed
 * after the wipe (tape_ui_poll, once it has stopped). */
static void rec_clear(void);                            /* (ui_layer.c) */
static uint8_t retake_steps __attribute__((section(".pool")));   /* the retake under way cleared steps too */
static void tape_ui_retake(void)
{
    uint32_t k = song.sel % NTRK;
    int steps = !seq_is_empty(TSEL) || motion_count(TSEL), had = tape_count(k) != 0;
    if (!ui.home && page_fam(cur_page()) == FAM_SEQ) {
        if (steps || !had)
            rec_clear();                                /* (nothing at all on the track: it says so) */
        if (had) {
            char b[4] = {'T', (char)('1' + k), 0, 0};
            tape_clear(1u << k);
            ui_say(b, steps ? ": STEPS + TAPE GONE" : ": TAPE CLEARED");   /* (a message: 23 letters) */
            str_cpy(ui.msg2, steps ? "HOLD [SAVE] 2X: UNDO" : "HOLD [SAVE]: UNDO", sizeof ui.msg2);
        }
        return;
    }
    if (steps)
        rec_clear();
    retake_steps = (uint8_t)steps;
    if (song.playing || seq_counting())
        transport_req = 2;
    tape.q_retake = (uint8_t)(1u + k);
}
/* every frame (ui_input): the pool ran out during a take, said once; which undo is the newest; a retake carried out */
static void tape_ui_poll(void)
{
    static uint8_t said __attribute__((section(".pool"))), useen __attribute__((section(".pool")));
    if (tape.full && !said)
        ui_message(tape.full == TF_NOTES ? "TAPE FULL" : tape.full == TF_RUNS ? "STOP TO GO ON" : "TAPE AT ITS END");
    said = tape.full;
    if (tape.ugen != useen) {
        useen = tape.ugen;
        tape_useq = upin.seq;
    }
    if ((tape.q_retake & 0x80u) && !tape.q_clr) {       /* .. its wipe is done (it ended the armed take): a take armed */
        tape.q_retake = 0;
        if (!tape_rec_on())
            tape_rec_toggle();
        ui.force = 1;
    } else if (tape.q_retake && !(tape.q_retake & 0x80u) && !song.playing && !seq_counting() && !transport_req &&
               !tape.run && !tape.q_clr) {
        uint32_t k = (tape.q_retake - 1u) % NTRK;
        char b[4] = {'T', (char)('1' + k), 0, 0};
        int had = tape_count(k) != 0;
        tape.q_retake |= 0x80u;
        if (had)
            tape_clear(1u << k);
        if (had || retake_steps) {
            ui_say(b, " CLEARED: PLAY AGAIN");
            str_cpy(ui.msg2, had && retake_steps ? "HOLD [SAVE] 2X: UNDO" : "HOLD [SAVE]: UNDO", sizeof ui.msg2);
        }
        retake_steps = 0;
    }
}
