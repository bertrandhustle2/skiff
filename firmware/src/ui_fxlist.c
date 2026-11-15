/* SPDX-License-Identifier: GPL-3.0-only
 * (the new layout) FX's first page: the selected track's effects as a list, one a row: whether it is on, what it is
 * set to, how much of it. The four knobs are the page's own, as ever (DIST, CHORUS, DELAY, REVERB: how much the track
 * sends); each effect's own page comes after this one (FX again, or SELECT). DELAY and REVERB are the song's, one of
 * each for the six tracks: a track has its share of them, not a copy. Included by ui_graph.c (after ui_pgraph.c). */
static const page_t *fxl_page(uint32_t g)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == g && PAGES[i].fam == FAM_FX)
            return &PAGES[i];
    return 0;
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
    uint32_t i;
    (void)t;
    for (i = 0; i < 7u; i++) {
        char amt[20], what[20], num[2] = {(char)('1' + i), 0};
        int32_t y = 3 + 19 * (int32_t)i;
        int on_w = R[i].what != 0xFFu ? fxl_val(R[i].g, R[i].what, what, sizeof what) : 0;
        int on = R[i].amt != 0xFFu ? fxl_val(R[i].g, R[i].amt, amt, sizeof amt) : on_w;
        if (R[i].g == GR_DUCK)                          /* (DUCK: under no track it does nothing, whatever its depth) */
            on = on && on_w;
        if (R[i].what == 0xFFu)
            what[0] = 0;
        if (R[i].amt == 0xFFu)
            str_cpy(amt, on ? "ON" : "", sizeof amt);
        cv_rrect(10, y + 3, 5, 11, 2, on ? c : T_LINE, T_SURF);
        pg_text(22, y + 14, num, T_DIM);
        pg_text(36, y + 14, R[i].name, on ? T_TEXT : T_DIM);
        if (on && what[0])
            pg_text(112, y + 14, what, T_MID);
        pg_text_r(228, y + 14, on ? amt : "OFF", on ? T_TEXT : T_DIM);
    }
}
