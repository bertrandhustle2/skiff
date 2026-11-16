/* SPDX-License-Identifier: GPL-3.0-only
 * (the new layout) FX's first page: the selected track's effects as a list, one a row: whether it is on, what it is
 * set to, how much of it. SELECT moves down the list before it goes on to the pages beside it (a sheet: ui.c sheet_t,
 * ui_input.c SHEETS): on the list's own row the four knobs are the page's, as ever (DIST, CHORUS, DELAY, REVERB: how
 * much the track sends); on an effect's row, lit, they are that effect's own four, the ones its page has. Each
 * effect's page, with its picture, comes after this one (FX again). DELAY, REVERB and CHORUS are the song's, one of
 * each for the six tracks: a track has its share of them, not a copy. Included by ui_graph.c (after ui_pgraph.c). */
static uint32_t layer_open(void);                       /* (ui_layer.c) */
static uint8_t fxl_win __attribute__((section(".pool")));    /* the row its knobs are: 0 the list's own, 1 .. 7 an effect's */
static uint8_t fxl_busy __attribute__((section(".pool")));   /* its row's knobs turned or drawn the plain way: no sheet */
#define FXL_ROWS 8u
static const page_t *fxl_page(uint32_t g)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == g && PAGES[i].fam == FAM_FX)
            return &PAGES[i];
    return 0;
}
/* the row shown (a quick layer held: the list's own, its sends) and the page whose four knobs it has */
static uint32_t fxl_row(void) { return UI_NEW && !layer_open() ? fxl_win % FXL_ROWS : 0u; }
static const page_t *fxl_row_page(uint32_t w)
{
    static const uint8_t G[FXL_ROWS] = {GR_FX, GR_DRIVE, GR_FX, GR_FXDLY, GR_FXREV, GR_INS, GR_SLCR, GR_DUCK};
    return w % FXL_ROWS == 2u ? &FXL_CHORUS : fxl_page(G[w % FXL_ROWS]);
}
/* slot s of FX's page g as its card writes it; 1: above its lowest (the effect is on) */
static int fxl_val(uint32_t g, uint32_t s, char *b, uint32_t n)
{
    const page_t *pg = fxl_page(g);
    int16_t *vp = 0;
    const param_desc_t *d = pg && s < 4u ? page_desc(pg, s, &vp) : 0;
    const char *u = "";
    char v[12];
    b[0] = 0;
    if (!d || !vp)
        return 0;
    param_format(d, *vp, v, &u);
    str_cpy(b, v, n);
    if (u[0] && str_len(b) + str_len(u) + 2u < n) {
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), u, n - str_len(b));
    }
    return *vp > d->min;
}
static void graph_fx_list(const track_t *t, uint16_t c)
{
    static const struct {
        const char *name;
        uint8_t g, amt, what;                           /* its page; the slot that says how much (0xFF none: on by `what`),
                                                         * the one that says what it is set to (0xFF none) */
    } R[7] = {{"DRIVE", GR_DRIVE, 0, 1}, {"CHORUS", GR_FX, 1, 0xFF}, {"DELAY", GR_FXDLY, 0, 1}, {"REVERB", GR_FXREV, 0, 1},
              {"INSERT", GR_INS, 0xFF, 0}, {"SLICER", GR_SLCR, 0, 2}, {"DUCK", GR_DUCK, 1, 0}};
    uint32_t i, row = fxl_row();
    (void)t;
    for (i = 0; i < 7u; i++) {
        char amt[20], what[20], num[2] = {(char)('1' + i), 0};
        int32_t y = 3 + 19 * (int32_t)i;
        int on_w = R[i].what != 0xFFu ? fxl_val(R[i].g, R[i].what, what, sizeof what) : 0;
        int on = R[i].amt != 0xFFu ? fxl_val(R[i].g, R[i].amt, amt, sizeof amt) : on_w, lit = row == i + 1u;
        uint16_t bg = lit ? T_TEXT : T_SURF;
        if (R[i].g == GR_DUCK)                          /* (DUCK: under no track it does nothing, whatever its depth) */
            on = on && on_w;
        if (R[i].what == 0xFFu)
            what[0] = 0;
        if (R[i].amt == 0xFFu)
            str_cpy(amt, on ? "ON" : "", sizeof amt);
        if (lit)                                        /* the row the four knobs are: a bar, its words in ink */
            cv_rect(4, y + 1, 232, 17, T_TEXT);
        cv_rrect(10, y + 3, 5, 11, 2, on ? (lit ? T_INK : c) : (lit ? T_MID : T_LINE), bg);
        cv_text_on(22, y + 14 - AF_S.asc, &AF_S, num, lit ? T_INK : T_DIM, bg);
        cv_text_on(36, y + 14 - AF_S.asc, &AF_S, R[i].name, lit ? T_INK : on ? T_TEXT : T_DIM, bg);
        if (on && what[0])
            cv_text_on(112, y + 14 - AF_S.asc, &AF_S, what, lit ? T_INK : T_MID, bg);
        cv_text_r(228, y + 14 - AF_S.asc, &AF_S, on ? amt : "OFF", lit ? T_INK : on ? T_TEXT : T_DIM, bg);
    }
}
/* what the list shows besides the track's own values (ui_graph.c graph_signature): its row, every row's words */
static uint32_t fxl_sig(void)
{
    static const uint8_t G[6] = {GR_DRIVE, GR_FXDLY, GR_FXREV, GR_INS, GR_SLCR, GR_DUCK};
    uint32_t h = 0xF11u + fxl_row() * 977u, i, s;
    char b[20];
    for (i = 0; i < 6u; i++)
        for (s = 0; s < 3u; s++) {
            (void)fxl_val(G[i], s, b, sizeof b);
            h = str_hash(h, b);
        }
    return h + (uint32_t)song.g[G_CMODE] * 31u + (uint32_t)song.g[G_CRATE] * 131u + (uint32_t)song.g[G_CDEPTH] * 7919u;
}
static const char *fxl_title(void)
{
    static const char *const T[FXL_ROWS] = {"ALL EFFECTS", "DRIVE", "CHORUS", "DELAY", "REVERB", "INSERT", "SLICER", "DUCK"};
    return T[fxl_row()];
}
