/* SPDX-License-Identifier: GPL-3.0-only
 * The designer (the new layout: tape.c UI_NEW): EDIT is where a sound is made, in the manner of Xfer's Serum and
 * Shrub, and EDIT held writes it onto the selected track.
 *
 * Its sound is a WAVE sound (eng_wave.c: two oscillators with a table each, a sub, noise, a filter). Its five pages,
 * all under EDIT (nine screens: a page's rows of four knobs one after the other, then the next page; SELECT the same,
 * both ways): SOUND (both waves side by side), OSC A, OSC B, SUB NOISE, FILTER: the list is at "the pages" below.
 * Everything else a sound has, where it always was: ENV, LFO, FX. PRESETS steps through WAVE's sounds, to start from
 * one.
 *
 * EDIT tapped on a track that plays another kind of sound does not change that track: the designer's sound is tried
 * on it (the keys and the track's tape play it; the track's own sound is kept aside) until EDIT is held, which keeps
 * it there (one undo: SAVE held), or until HOME, a SEQ or SAVE page, or another track, which put the track's own
 * sound back. The designer's sound stays the designer's: EDIT on another track tries the same one there, and EDIT
 * held anywhere writes it onto the selected track. On a track that has a WAVE sound, EDIT edits that sound itself,
 * and the designer's sound is that one from then on. Tracks 5 and 6 cannot play it (WAVE needs a heavy track's room).
 *
 * What EDIT held before (Skiff's own layout: the sound's two pages, FM6's and the drums' sheets, VOICE): under FX,
 * after its pages; a filter's page (ANALOG's) and the FM operators' envelopes under ENV (ui.c page_fam).
 * Included by ui_input.c (before SHEETS). */
static void autosave_soon(void);                        /* (project.c) */
static struct {
    undo_t c;                        /* no trial: the designer's sound (its engine, preset, values); a trial: the
                                      * track's own sound, kept aside (undo_swap puts it back) */
    uint16_t seq0;                   /* the undo ring's newest copy when the trial began: later ones are the trial's */
    uint8_t live;                    /* c holds a sound (else: WAVE's first, when asked for) */
    uint8_t src;                     /* 1 + the track whose own sound the designer showed last: c follows it */
    uint8_t shown;                   /* 1 + the track the designer's pages show now (its own WAVE sound, no trial) */
    uint8_t win[5];                  /* the pages' rows of four knobs: the one each shows (SOUND, OSC A, OSC B, SUB, FILTER) */
    uint8_t last;                    /* 1 + the knob turned last on the page shown: its words on the strip, its dial lit */
    uint8_t at;                      /* .. on which page and row (1 + its row in DWIN) */
    uint32_t strip;                  /* what the strip shows (design_cards: drawn when it changes) */
} ds __attribute__((section(".pool")));

static int design_is(const track_t *t) { return t->eng_req % NENGINES == ENGI_WAVE; }
/* (ui.c design_trk: 1 + the track the designer's sound is tried on; 0: no trial) */
/* a page the trial lasts on: every page but SEQ's and SAVE's (HOME ends it too: design_poll) */
static int design_stays(const page_t *pg) { return page_fam(pg) != FAM_SEQ && page_fam(pg) != FAM_SAVE; }

static void design_first(void)                            /* nothing designed yet: WAVE's first sound */
{
    memset(&ds.c, 0, sizeof ds.c);
    ds.c.what = UNDO_SOUND;
    ds.c.eng = ENGI_WAVE;
    preset_fill(ds.c.p, ENGINES[ENGI_WAVE], 0);
    ds.live = 1;
}
/* the designer's sound is track k's (a WAVE one), as it is now */
static void design_grab(uint32_t k)
{
    const track_t *t = &trk[k % NTRK];
    uint32_t i;
    memset(&ds.c, 0, sizeof ds.c);
    ds.c.what = UNDO_SOUND;
    ds.c.eng = t->eng_req;
    ds.c.preset = t->preset;
    for (i = 0; i < P_COUNT; i++)
        ds.c.p[i] = (int16_t)motion_base_value(t, i);
    ds.live = 1;
}
/* the copies the undo ring took since the trial began were the trial's own (a preset stepped through): gone, and the
 * tape's last take or wipe is the newest thing to undo again if it was made meanwhile (ui_tape.c tape_undo_newest) */
static void design_undo_drop(void)
{
    while (undo_n && (uint16_t)(undo.seq - ds.seq0 - 1u) < 0x8000u) {
        undo.trk = 0;
        undo.seq = 0;
        undo_top = (uint8_t)((undo_top + UNDO_N - 1u) % UNDO_N);
        undo_n--;
        if (undo_back)
            undo_back--;
    }
    if ((uint16_t)(tape_useq - ds.seq0 - 1u) < 0x8000u)
        tape_useq = ds.seq0;
    upin.seq = ds.seq0;
}
static void design_say(const char *a, uint32_t k, const char *b)   /* "a" + the track's number + "b" */
{
    char s[32];
    str_cpy(s, a, sizeof s);
    s[str_len(s) + 1u] = 0;
    s[str_len(s)] = (char)('1' + k % NTRK);
    str_cpy(s + str_len(s), b, sizeof s - str_len(s));
    ui_message(s);
}

/* the designer's sound tried on track k (another kind of sound's): 1 begun */
static int design_begin(uint32_t k)
{
    if (ds.src && design_is(&trk[ds.src - 1u]))           /* (the sound the designer showed last, as it is now) */
        design_grab(ds.src - 1u);
    else if (!ds.live)
        design_first();
    ds.src = 0;
    memset(&ds.c.motion_backup, 0, sizeof ds.c.motion_backup);   /* (the designer's sound has no automation) */
    memcpy(ds.c.fm6, fm6_patch[k], FP_SIZE);              /* (the track's FM6 patch stays where it is) */
    ds.c.fm6_slot = fm6_slot[k];
    ds.c.trk = (uint8_t)(k + 1u);
    ds.c.what = UNDO_SOUND;
    ds.seq0 = upin.seq;
    if (!undo_swap(&ds.c))                                /* (said why not) */
        return 0;
    design_trk = (uint8_t)(k + 1u);
    design_say("TRYING IT ON T", k, "");
    str_cpy(ui.msg2, "HOLD [EDIT]: KEEP IT", sizeof ui.msg2);
    return 1;
}
/* the trial over, not kept: the track's own sound back, the designer's sound as it was left */
static void design_end(void)
{
    uint32_t k = (uint32_t)design_trk - 1u;
    if (!design_trk)
        return;
    design_trk = 0;
    if (!design_is(&trk[k % NTRK])) {                     /* another sound was loaded onto it meanwhile: that one stays */
        ds.live = 0;                                      /* (c is what the track had: no designer's sound any more) */
        design_undo_drop();
        return;
    }
    if (!undo_swap(&ds.c)) {                              /* (the automation's pool full: the sound without it) */
        memset(&ds.c.motion_backup, 0, sizeof ds.c.motion_backup);
        (void)undo_swap(&ds.c);
    }
    design_undo_drop();
    design_say("T", k, ": ITS OWN SOUND BACK");
}
/* EDIT held: the designer's sound onto the selected track */
static void design_hold(void)
{
    uint32_t k = song.sel % NTRK, i;
    track_t *t = &trk[k];
    if (design_trk == k + 1u) {                                /* tried there: kept. The undo copy is the track as it was */
        uint16_t seq;
        const char *nm;
        design_trk = 0;
        design_undo_drop();
        undo_label = "DESIGN";
        undo_push(UNDO_SOUND);
        seq = undo.seq;
        nm = undo.name;
        undo = ds.c;
        undo.seq = seq;
        undo.name = nm;
        undo.trk = (uint8_t)(k + 1u);
        undo.what = UNDO_SOUND;
        undo.after = track_sig(t);
        undo.keep = 0;
        undo.t_ms = fm1_ms;
        design_grab(k);
        ds.src = (uint8_t)(k + 1u);
        autosave_soon();
        design_say("T", k, " HAS THE NEW SOUND");
        str_cpy(ui.msg2, "HOLD [SAVE]: UNDO", sizeof ui.msg2);
        return;
    }
    if (ds.src && design_is(&trk[ds.src - 1u]))
        design_grab(ds.src - 1u);
    if (ds.src == k + 1u) {
        design_say("T", k, " ALREADY HAS IT");
        return;
    }
    if (!ds.live) {
        ui_message("NOTHING DESIGNED YET");
        str_cpy(ui.msg2, "[EDIT]: DESIGN A SOUND", sizeof ui.msg2);
        return;
    }
    if (!eng_allowed(k, ENGI_WAVE)) {
        ui_message("DESIGNER: TRACKS 1-4");
        return;
    }
    undo_label = "DESIGN";                                /* written there as a sound load: one undo copy */
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << k);
    fm1_irq_off();
    t->eng_req = ds.c.eng;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = ds.c.p[i];
    t->preset = ds.c.preset;
    t->user = 0;
    fm1_irq_on();
    load_end(t);
    undo.keep = 0;                                        /* (a sound stepped to after it: its own undo) */
    sync_reload = 1;
    ui.force = 1;
    autosave_soon();
    design_say("T", k, " HAS THE NEW SOUND");
    str_cpy(ui.msg2, "HOLD [SAVE]: UNDO", sizeof ui.msg2);
}
/* EDIT tapped: the designer's pages (the next one, from one of them); on a track with another kind of sound, its
 * sound tried there first. On a step page with a step held or the track armed, EDIT keeps its step jobs (page_tap) */
static void design_tap(void)
{
    uint32_t k = song.sel % NTRK;
    if (song.seq_mode && !ui.home && step_page(cur_page()->graph) && (lock_held() || ((song.rec >> song.sel) & 1u))) {
        (void)page_tap(B_EDIT);
        return;
    }
    if (!design_is(&trk[k])) {
        if (!eng_allowed(k, ENGI_WAVE)) {
            ui_message("DESIGNER: TRACKS 1-4");
            return;
        }
        if (!design_begin(k))
            return;
    }
    {   /* from one of its pages: the page's next row of knobs, then the next page at its first */
        int in = !ui.home && GR_DESIGN(cur_page()->graph);
        const sheet_t *sh = in ? sheet_of(cur_page()->graph) : 0;
        if (sh && sheet_rows(sh, 1)) {
            ui.force = 1;
            return;
        }
        (void)page_tap(B_EDIT);
        if (in && !ui.home && GR_DESIGN(cur_page()->graph))
            ds.win[cur_page()->graph - GR_DSND] = 0;
    }
}
/* every pass (ui_input): a trial ends where it does not last; the designer's sound follows the WAVE track its pages
 * were on, as that track is when they are left */
static void design_poll(void)
{
    uint32_t k = song.sel % NTRK, now;
    if (design_trk && (ui.home || design_trk != k + 1u || !design_stays(cur_page())))
        design_end();
    now = !design_trk && !ui.home && page_fam(cur_page()) == FAM_EDIT && design_is(&trk[k]) ? k + 1u : 0u;
    if (ds.shown && ds.shown != now && design_is(&trk[ds.shown - 1u])) {
        design_grab(ds.shown - 1u);
        ds.src = ds.shown;
    }
    if (now)
        ds.src = (uint8_t)now;
    ds.shown = (uint8_t)now;
}

/* ---------------------------------------------------------------------------------------------- the pages --- */
/* Five pages, EDIT stepping through them, a page's rows of four knobs before the next page (SELECT the same, both
 * ways):
 *   SOUND      WAVE A  WAVE B  A LEVEL  B LEVEL            both waves side by side
 *   OSC A      POS  WARP  AMOUNT  LEVEL                    its wave as it sounds
 *              OCTAVE  SEMI  FINE  THICK
 *   OSC B      the same
 *   SUB NOISE  SUB  SHAPE  NOISE  COLOUR
 *   FILTER     CUTOFF  RES  DRIVE  KIND                    its curve
 *              WAVE A  WAVE B  NOISE  SUB                  what goes through it
 *              ENV AMT  KEY TRK
 * A page: where the cards are on every other page, a strip that says in plain words what the knob turned last does;
 * the panel's picture; under it a dial for each of the four knobs, its name and its value. */
enum { DK_TAB_A, DK_TAB_B, DK_LVL_A, DK_LVL_B,
       DK_POS_A, DK_KIND_A, DK_AMT_A, DK_OCT_A, DK_SEMI_A, DK_FINE_A, DK_THICK_A,
       DK_POS_B, DK_KIND_B, DK_AMT_B, DK_OCT_B, DK_SEMI_B, DK_FINE_B, DK_THICK_B,
       DK_SUB, DK_SUBK, DK_NOISE, DK_NCOL, DK_CUT, DK_RES, DK_DRIVE, DK_FKIND,
       DK_THRU_A, DK_THRU_B, DK_THRU_N, DK_THRU_S, DK_ENV, DK_KTRK, DK_NONE };
static const uint8_t DWIN[][4] = {
    {DK_TAB_A, DK_TAB_B, DK_LVL_A, DK_LVL_B},
    {DK_POS_A, DK_KIND_A, DK_AMT_A, DK_LVL_A},
    {DK_OCT_A, DK_SEMI_A, DK_FINE_A, DK_THICK_A},
    {DK_POS_B, DK_KIND_B, DK_AMT_B, DK_LVL_B},
    {DK_OCT_B, DK_SEMI_B, DK_FINE_B, DK_THICK_B},
    {DK_SUB, DK_SUBK, DK_NOISE, DK_NCOL},
    {DK_CUT, DK_RES, DK_DRIVE, DK_FKIND},
    {DK_THRU_A, DK_THRU_B, DK_THRU_N, DK_THRU_S},
    {DK_ENV, DK_KTRK, DK_NONE, DK_NONE},
};
static const uint8_t DROW0[5] = {0, 1, 3, 5, 6};          /* a page's first row in DWIN */
/* a control: the dial's name, the strip's, what it does in plain words; its stored value (0xFF: a part of one) and
 * the range it is turned over */
typedef struct {
    const char *lab, *name, *hint;
    uint8_t id;
    int8_t lo;
    uint8_t hi;
} dk_t;
static const dk_t DK[DK_NONE] = {
    [DK_TAB_A] = {"WAVE A", "WAVE A", "WHICH SET OF SHAPES A PLAYS", 0xFF, 0, 7},
    [DK_TAB_B] = {"WAVE B", "WAVE B", "WHICH SET OF SHAPES B PLAYS", 0xFF, 0, 7},
    [DK_LVL_A] = {"A LEVEL", "A LEVEL", "HOW LOUD WAVE A IS", WX(WX_ALVL), 0, 127},
    [DK_LVL_B] = {"B LEVEL", "B LEVEL", "HOW LOUD WAVE B IS", WX(WX_BLVL), 0, 127},
    [DK_POS_A] = {"POS", "POSITION", "WHERE IN THE SET: THE SHAPE", P_E1, 0, 127},
    [DK_KIND_A] = {"WARP", "WARP", "", 0xFF, 0, WK_N - 1},
    [DK_AMT_A] = {"AMOUNT", "WARP AMOUNT", "HOW FAR THE WARP GOES", P_E6, 0, 127},
    [DK_OCT_A] = {"OCTAVE", "OCTAVE", "12 NOTES UP OR DOWN A STEP", 0xFF, -3, 3},
    [DK_SEMI_A] = {"SEMI", "SEMITONES", "ONE NOTE UP OR DOWN A STEP", 0xFF, -12, 12},
    [DK_FINE_A] = {"FINE", "FINE TUNE", "A TOUCH OFF: 100 IS ONE NOTE", 0xFF, -63, 63},
    [DK_THICK_A] = {"THICK", "THICK", "2 COPIES A LITTLE OFF: FATTER", WX(WX_ATHICK), 0, 127},
    [DK_POS_B] = {"POS", "POSITION", "WHERE IN THE SET: THE SHAPE", P_E2, 0, 127},
    [DK_KIND_B] = {"WARP", "WARP", "", 0xFF, 0, WK_N - 1},
    [DK_AMT_B] = {"AMOUNT", "WARP AMOUNT", "HOW FAR THE WARP GOES", WX(WX_BAMT), 0, 127},
    [DK_OCT_B] = {"OCTAVE", "OCTAVE", "12 NOTES UP OR DOWN A STEP", 0xFF, -3, 3},
    [DK_SEMI_B] = {"SEMI", "SEMITONES", "ONE NOTE UP OR DOWN A STEP", 0xFF, -12, 12},
    [DK_FINE_B] = {"FINE", "FINE TUNE", "A TOUCH OFF: 100 IS ONE NOTE", 0xFF, -63, 63},
    [DK_THICK_B] = {"THICK", "THICK", "2 COPIES A LITTLE OFF: FATTER", WX(WX_BTHICK), 0, 127},
    [DK_SUB] = {"SUB", "SUB", "A LOW NOTE UNDER THE SOUND", P_E3, 0, 127},
    [DK_SUBK] = {"SHAPE", "SUB SHAPE", "THE LOW NOTE'S OWN SHAPE", WX(WX_SUBK), 0, WS_N - 1},
    [DK_NOISE] = {"NOISE", "NOISE", "HISS MIXED IN", WX(WX_NLVL), 0, 127},
    [DK_NCOL] = {"COLOUR", "NOISE COLOUR", "THE HISS: A RUMBLE .. BRIGHT", WX(WX_NCOL), 0, 127},
    [DK_CUT] = {"CUTOFF", "CUTOFF", "WHERE THE FILTER CUTS", P_E4, 0, 127},
    [DK_RES] = {"RES", "RESONANCE", "A WHISTLE AT THE CUTOFF", P_E5, 0, 127},
    [DK_DRIVE] = {"DRIVE", "DRIVE", "PUSHED INTO GRIT", P_E7, 0, 127},
    [DK_FKIND] = {"KIND", "FILTER KIND", "", 0xFF, 0, WF_N - 1},
    [DK_THRU_A] = {"WAVE A", "WAVE A", "ON: THROUGH THE FILTER", 0xFF, 0, 1},
    [DK_THRU_B] = {"WAVE B", "WAVE B", "ON: THROUGH THE FILTER", 0xFF, 0, 1},
    [DK_THRU_N] = {"NOISE", "NOISE", "ON: THROUGH THE FILTER", 0xFF, 0, 1},
    [DK_THRU_S] = {"SUB", "SUB", "ON: THROUGH THE FILTER", 0xFF, 0, 1},
    [DK_ENV] = {"ENV AMT", "ENVELOPE AMOUNT", "EACH NOTE OPENS THE FILTER", P_ED_FLT, -64, 63},
    [DK_KTRK] = {"KEY TRK", "KEY TRACK", "HIGH NOTES: A HIGHER CUTOFF", WX(WX_KTRK), 0, 127},
};
static const char *const DK_WARP_HINT[WK_N] = {
    "THE SHAPE AS IT IS", "PLAYED FASTER: A HARD EDGE", "SQUEEZED TOWARD ITS START", "SQUEEZED TOWARD ITS END",
    "SQUEEZED, THEN SILENCE: THIN", "SHAKEN BY THE OTHER WAVE"};
static const char *const DK_FLT_HINT[WF_N] = {
    "CUTS THE HIGHS, GENTLY", "CUTS THE HIGHS, HARD", "KEEPS AROUND THE CUTOFF", "CUTS THE LOWS", "CUTS A SLICE OUT"};
static const char *const DK_PAGE_HINT[NELEM(DWIN)] = {
    "PICK THE TWO WAVES, HOW LOUD", "THE WAVE AS YOU HEAR IT", "ITS PITCH BESIDE THE NOTE", "THE WAVE AS YOU HEAR IT",
    "ITS PITCH BESIDE THE NOTE", "A LOW NOTE AND HISS UNDER IT", "TAKES PART OF THE SOUND AWAY",
    "WHAT GOES THROUGH THE FILTER", "WHAT MOVES THE CUTOFF"};

static uint32_t design_row(void)                          /* the page's row of knobs shown: its place in DWIN */
{
    uint32_t g = cur_page()->graph, i = GR_DESIGN(g) ? g - GR_DSND : 0u;
    static const uint8_t NW[5] = {1, 2, 2, 1, 3};
    return DROW0[i] + ds.win[i] % NW[i];
}
static uint32_t design_ctl(uint32_t k) { return DWIN[design_row()][k & 3u]; }
static int dk_b(uint32_t c) { return c >= DK_POS_B && c <= DK_THICK_B; }   /* B's (among the two oscillators' own) */
static int32_t dk_get(const int16_t *p, uint32_t c)
{
    uint32_t o = (uint32_t)dk_b(c);
    switch (c) {
    case DK_TAB_A: return (int32_t)wv_tab(p, 0);
    case DK_TAB_B: return (int32_t)wv_tab(p, 1);
    case DK_KIND_A: case DK_KIND_B: return (int32_t)wv_kind(p, o);
    case DK_OCT_A: case DK_OCT_B: return wv_oct(p, o);
    case DK_SEMI_A: case DK_SEMI_B: return wv_semi(p, o);
    case DK_FINE_A: case DK_FINE_B: return clamp(wv_fine(p, o), -63, 63);
    case DK_FKIND: return (int32_t)wv_fkind(p);
    case DK_THRU_A: case DK_THRU_B: case DK_THRU_N: case DK_THRU_S: return (int32_t)((wv_through(p) >> (c - DK_THRU_A)) & 1u);
    default: return clamp(p[DK[c].id], DK[c].lo, DK[c].hi);
    }
}
static void design_set(uint32_t id, int32_t v)            /* a stored value, as a knob sets one (its automation too) */
{
    TSEL->p[id] = (int16_t)v;
    motion_capture(TSEL, id, TSEL->p[id]);
    ml_knob(TSEL, id);
}
static void dk_put(uint32_t c, int32_t v)
{
    const int16_t *p = TSEL->p;
    uint32_t o = (uint32_t)dk_b(c);
    switch (c) {
    case DK_TAB_A: design_set(P_E0, (int32_t)WVT((uint32_t)v, wv_tab(p, 1))); break;
    case DK_TAB_B: design_set(P_E0, (int32_t)WVT(wv_tab(p, 0), (uint32_t)v)); break;
    case DK_KIND_A: case DK_KIND_B: design_set(WX(WX_AWARP + o), WVA(v, wv_oct(p, o))); break;
    case DK_OCT_A: case DK_OCT_B: design_set(WX(WX_AWARP + o), WVA((int32_t)wv_kind(p, o), v)); break;
    case DK_SEMI_A: case DK_SEMI_B: design_set(WX(WX_ASEMI + o), v + 12); break;
    case DK_FINE_A: case DK_FINE_B: design_set(WX(WX_AFINE + o), v + 64); break;
    case DK_FKIND: design_set(WX(WX_FLT), (int32_t)WVF((uint32_t)v, wv_through(p))); break;
    case DK_THRU_A: case DK_THRU_B: case DK_THRU_N: case DK_THRU_S: {
        uint32_t bit = 1u << (c - DK_THRU_A), th = wv_through(p);
        design_set(WX(WX_FLT), (int32_t)WVF(wv_fkind(p), v ? th | bit : th & ~bit));
        break;
    }
    default: design_set(DK[c].id, v); break;
    }
}
/* a control's value as its dial says it */
static void dk_text(uint32_t c, int32_t v, char *b, const char **u)
{
    *u = "";
    switch (c) {
    case DK_TAB_A: case DK_TAB_B: str_cpy(b, N_WV_TABLE[(uint32_t)v % WV_NTAB], 12); return;
    case DK_KIND_A: case DK_KIND_B: str_cpy(b, N_WV_KIND[(uint32_t)v % WK_N], 12); return;
    case DK_SUBK: str_cpy(b, N_WV_SUB[(uint32_t)v % WS_N], 12); return;
    case DK_FKIND: str_cpy(b, N_WV_FLT[(uint32_t)v % WF_N], 12); return;
    case DK_THRU_A: case DK_THRU_B: case DK_THRU_N: case DK_THRU_S: str_cpy(b, v ? "ON" : "OFF", 12); return;
    case DK_CUT: param_format(&ENG_WAVE.edit[4], v, b, u); return;
    case DK_ENV: param_format(&TP[P_ED_FLT], v, b, u); return;
    case DK_KTRK: fmt_int(b, v * 100 / 64); *u = "%"; return;
    case DK_OCT_A: case DK_OCT_B: case DK_SEMI_A: case DK_SEMI_B: case DK_FINE_A: case DK_FINE_B:
        b[0] = '+';
        fmt_int(b + (v > 0), v);
        if (c == DK_FINE_A || c == DK_FINE_B)
            *u = "CT";
        return;
    default:
        if (!v && (c == DK_AMT_A || c == DK_AMT_B || c == DK_THICK_A || c == DK_THICK_B || c == DK_SUB || c == DK_NOISE ||
                   c == DK_DRIVE)) {
            str_cpy(b, "OFF", 12);
            return;
        }
        fmt_int(b, (v * 100 + 63) / 127);
        *u = "%";
        return;
    }
}
static void design_knob(uint32_t k, int32_t s)
{
    uint32_t c = design_ctl(k);
    int32_t lo, hi, v;
    if (!design_is(TSEL) || c == DK_NONE)                 /* (the sound changed under the page: ui_draw leaves it) */
        return;
    ds.last = (uint8_t)(1u + (k & 3u));
    ds.at = (uint8_t)(1u + design_row());
    lo = DK[c].lo;
    hi = DK[c].hi;
    v = dk_get(TSEL->p, c);
    v = clamp(v + (hi - lo > 30 ? accel(EN_K1 + k, s, hi - lo) : s), lo, hi);
    if (v != dk_get(TSEL->p, c))
        dk_put(c, v);
}
/* the knob turned last on the row shown: 0 .. 3, else -1 */
static int32_t design_hot(void) { return ds.last && ds.at == 1u + design_row() ? (int32_t)ds.last - 1 : -1; }
static const char *design_title(void)
{
    static const char *const T[5] = {"SOUND", "OSC A", "OSC B", "SUB NOISE", "FILTER"};
    uint32_t g = cur_page()->graph;
    return T[GR_DESIGN(g) ? g - GR_DSND : 0u];
}
/* the strip where the cards are: the knob turned last, its name and value, and what it does; none yet: the page's */
static void design_cards(void)
{
    const int16_t *p = TSEL->p;
    int32_t hot = design_hot();
    uint32_t r = design_row(), c = hot >= 0 ? DWIN[r][hot] : DK_NONE, h, i, nw;
    char val[16] = "";
    const char *unit = "", *a = design_title(), *b = DK_PAGE_HINT[r];
    if (c != DK_NONE) {
        int32_t v = dk_get(p, c);
        dk_text(c, v, val, &unit);
        a = DK[c].name;
        b = c == DK_KIND_A || c == DK_KIND_B ? DK_WARP_HINT[(uint32_t)v % WK_N] : c == DK_FKIND ? DK_FLT_HINT[(uint32_t)v % WF_N]
                                                                                                : DK[c].hint;
    }
    h = str_hash(str_hash(str_hash(str_hash(0x5717u + r * 31u + (uint32_t)ux.gen * 977u + song.sel * 7919u, a), b), val), unit);
    if (!ui.force && h == ds.strip)
        return;
    ds.strip = h;
    for (i = 0; i < 4u; i++)                              /* (the cards of the page before: gone under it) */
        ui.col[i][0] = 0;
    cv_begin(234, CARD_H, T_BG);
    cv_rrect(0, 0, 234, CARD_H, 5, T_SURF, T_BG);
    cv_bg = T_SURF;
    cv_text_fit(8, 3, &AF_M, a, c != DK_NONE ? T_ACCENT : T_TEXT, T_SURF, 150);
    if (val[0]) {
        int32_t x = cv_text_r(226 - (unit[0] ? text_w(&AF_S, unit) + 3 : 0), 3, &AF_M, val, T_TEXT, T_SURF);
        (void)x;
        if (unit[0])
            cv_text_r(226, 3 + AF_M.asc - AF_S.asc, &AF_S, unit, T_MID, T_SURF);
    } else {                                              /* (no knob yet: which of the page's rows of knobs this is) */
        static const uint8_t NW[5] = {1, 2, 2, 1, 3};
        uint32_t g = cur_page()->graph - GR_DSND;
        nw = NW[g % 5u];
        for (i = 0; i < nw && nw > 1u; i++)
            cv_rrect(226 - (int32_t)(nw - i) * 9, 9, 6, 6, 3, i == ds.win[g % 5u] % nw ? T_TEXT : T_LINE, T_SURF);
    }
    cv_text_fit(8, 23, &AF_S, b, T_MID, T_SURF, 218);
    cv_blit(3, Y_LABEL);
}
/* what the strip and the panel show besides the track's values (ui_graph.c graph_signature hashes those) */
static uint32_t design_sig(void)
{
    return wave_sig(TSEL) * 31u + (uint32_t)design_trk * 977u + design_row() * 131u + (uint32_t)(design_hot() + 1) * 65537u;
}

/* a dial: a ring open at the bottom (270 degrees), lit from a0 to a1 (/ 1000 of the way round), a hand at a1 */
static void dial_pt(int32_t cx, int32_t cy, int32_t r, int32_t a, int32_t *x, int32_t *y)
{
    /* 225 degrees, clockwise. The angle is counted upwards and the circle mirrored: counted downwards, the step is
     * a negative number that build.py's check takes for an address in the chip's ROM (0xFFCxxxxx) */
    uint32_t ph = (uint32_t)a * 3221225u;
    *x = cx + ((sine_i(ph + 0xA0000000u) * r + 16384) >> 15);
    *y = cy - ((-sine_i(ph + 0x60000000u) * r + 16384) >> 15);
}
static void dial(int32_t cx, int32_t cy, int32_t a0, int32_t a1, uint16_t lit, uint16_t hand)
{
    int32_t i, x0, y0, x1, y1;
    if (a0 > a1) {
        i = a0;
        a0 = a1;
        a1 = i;
        i = 1;
    } else
        i = 0;
    {
        int32_t at = i ? a0 : a1, k;
        dial_pt(cx, cy, 12, 0, &x0, &y0);
        for (k = 1; k <= 18; k++) {
            int32_t m = (2 * k - 1) * 1000 / 36;
            dial_pt(cx, cy, 12, k * 1000 / 18, &x1, &y1);
            cv_line_t(x0, y0, x1, y1, m >= a0 && m <= a1 ? lit : T_LINE, 2);
            x0 = x1;
            y0 = y1;
        }
        dial_pt(cx, cy, 4, at, &x0, &y0);
        dial_pt(cx, cy, 10, at, &x1, &y1);
        cv_line_t(x0, y0, x1, y1, hand, 2);
    }
}
/* the four dials under the picture: each knob's, its name, its value */
static void design_dials(const track_t *t, uint16_t c)
{
    int32_t hot = design_hot();
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        uint32_t ctl = design_ctl(k);
        int32_t cx = 32 + 59 * (int32_t)k, v, a, w;
        char val[16];
        const char *unit;
        const aafont_t *f = &AF_S;
        if (ctl == DK_NONE)
            continue;
        v = dk_get(t->p, ctl);
        a = (v - DK[ctl].lo) * 1000 / (DK[ctl].hi - DK[ctl].lo);
        dial(cx, 98, DK[ctl].lo < 0 ? 500 : 0, a, (int32_t)k == hot ? T_ACCENT : c, (int32_t)k == hot ? T_ACCENT : T_TEXT);
        w = text_w(&AF_S, DK[ctl].lab);
        cv_text(cx - w / 2, 109, &AF_S, DK[ctl].lab, (int32_t)k == hot ? T_ACCENT : T_MID);
        dk_text(ctl, v, val, &unit);
        if (unit[0] && str_len(val) + str_len(unit) < sizeof val - 1u) {   /* ("35 %": the unit after it) */
            str_cpy(val + str_len(val), " ", 2);
            str_cpy(val + str_len(val), unit, 8);
        }
        w = text_w(f, val);
        cv_text(cx - w / 2, 122, f, val, T_TEXT);
    }
}
/* a picture's frame: its name top left, a line across its middle (the waves swing DS_H either side of it at most) */
#define DS_MID 51
#define DS_H 27
static void design_box(int32_t x, int32_t w, const char *name, uint16_t nc)
{
    cv_rect(x, DS_MID, w, 1, T_LINE);
    pg_text(x, 14, name, nc);
}
/* the panel: the page's picture, then the dials */
static void design_draw(const track_t *t, uint16_t c)
{
    const int16_t *p = t->p;
    uint32_t g = cur_page()->graph, r = design_row(), o = g == GR_DOSCB;
    char nm[20];
    switch (g) {
    case GR_DSND:                                         /* both waves, side by side: a dim one is silent */
        for (o = 0; o < 2u; o++) {
            int32_t x = o ? 124 : 10, lv = wv_x(p, WX_ALVL + o);
            str_cpy(nm, o ? "B  " : "A  ", sizeof nm);
            str_cpy(nm + 3, N_WV_TABLE[wv_tab(p, o) % WV_NTAB], sizeof nm - 3u);
            design_box(x, 106, nm, lv ? T_TEXT : T_DIM);
            wave_flat(t, o, x, DS_MID, 106, 15 + lv * 12 / 127, lv ? (o ? T_ACCENT : c) : T_DIM, 2, 0);
        }
        break;
    case GR_DOSCA:
    case GR_DOSCB: {
        int32_t th = wv_x(p, WX_ATHICK + o), d = 2 + th * 6 / 127;
        str_cpy(nm, N_WV_TABLE[wv_tab(p, o) % WV_NTAB], sizeof nm);
        design_box(10, 220, nm, T_MID);
        if (r == DROW0[g - GR_DSND] + 1u) {               /* the pitch row: THICK's copies beside it, the pitch in words */
            char b[24];
            int32_t nt = wv_oct(p, o) * 12 + wv_semi(p, o);   /* in notes, as the keys count them */
            if (th) {
                wave_flat(t, o, 10, DS_MID, 220, DS_H, T_DIM, 1, d);
                wave_flat(t, o, 10, DS_MID, 220, DS_H, T_DIM, 1, -d);
            }
            b[0] = '+';
            fmt_int(b + (nt > 0), nt);
            str_cpy(b + str_len(b), nt == 1 || nt == -1 ? " NOTE" : " NOTES", 8);
            pg_text_r(230, 14, nt ? b : wv_fine(p, o) ? "A TOUCH OFF THE NOTE" : "THE NOTE'S OWN PITCH", T_MID);
        }
        wave_flat(t, o, 10, DS_MID, 220, DS_H, o ? T_ACCENT : c, 2, 0);
        break;
    }
    case GR_DSUB:
        design_box(10, 106, "SUB", clamp(p[P_E3], 0, 127) ? T_TEXT : T_DIM);
        wave_sub_flat(t, 10, DS_MID, 106, DS_H - 3, c);
        design_box(124, 106, "NOISE", wv_x(p, WX_NLVL) ? T_TEXT : T_DIM);
        wave_noise_flat(t, 124, DS_MID, 106, DS_H - 3, T_TEXT);
        break;
    default: {                                            /* FILTER: its curve; the sources' row: which go through it */
        static const char *const SRC[4] = {"A", "B", "NOISE", "SUB"};
        uint32_t th = wv_through(p), k;
        wave_flt_curve(t, 10, 22, 220, 80, c);
        pg_text(10, 14, N_WV_FLT[wv_fkind(p)], T_MID);
        if (r == DROW0[4] + 1u) {
            int32_t x = 230;
            for (k = 4; k-- > 0u;) {
                x -= text_w(&AF_S, SRC[k]) + 8;
                pg_text(x, 14, SRC[k], (th >> k) & 1u ? T_TEXT : T_DIM);
            }
        } else {
            char b[16];
            const char *u;
            param_format(&ENG_WAVE.edit[4], clamp(p[P_E4], 0, 127), b, &u);
            str_cpy(b + str_len(b), " ", 2);
            str_cpy(b + str_len(b), u, 6);
            pg_text_r(230, 14, b, T_MID);
        }
        break;
    }
    }
    design_dials(t, c);
}
/* PRESETS on them: WAVE's sounds one by one, to start from (the ends where they are) */
static void design_pick(int32_t s)
{
    uint32_t n = ENG_WAVE.npresets;
    int32_t pi;
    if (!design_is(TSEL) || !n)
        return;
    pi = clamp((int32_t)(TSEL->preset % n) + s, 0, (int32_t)n - 1);
    if (pi != (int32_t)TSEL->preset || TSEL->user)
        apply_preset((uint32_t)pi);
}
