/* SPDX-License-Identifier: GPL-3.0-only
 * WAVE's own pictures (eng_wave.c), flat, one line each: an oscillator's cycle as it sounds now (the frames its POS
 * reads, through its WARP; while a note sounds, where the envelope and the LFO have moved POS), the SUB's shape, the
 * NOISE at its COLOUR, the filter's curve by its kind. The designer's pages draw with them (ui_design.c), and Skiff's
 * own EDIT > OSC (A's cycle). Included by ui_graph.c (after ui_pgraph.c: pg_text, pg_under, flt_level). */

/* oscillator o's POS as it plays (0 .. 127 << 8): a sounding voice's, else the knob's */
static int32_t wave_pos(const track_t *t, const wv_part_t *P, uint32_t o)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return P->pos[o & 1u];
    return clamp(t->p[o ? P_E2 : P_E1], 0, 127) << 8;
}
/* what the pictures show besides the sound's values (graph_signature) */
static uint32_t wave_sig(const track_t *t)
{
    const wv_part_t *P = wv_of(t);
    if (!P || t->engine != ENGI_WAVE)
        return 0x57A7u;
    return ((uint32_t)P->table[0] * 131u + (uint32_t)P->table[1] * 1543u + (uint32_t)P->ready[0] * 977u +
            (uint32_t)P->ready[1] * 15601u + (uint32_t)P->live) * 2654435761u +
           (uint32_t)(wave_pos(t, P, 0) >> 7) * 7919u + (uint32_t)(wave_pos(t, P, 1) >> 7) * 104729u;
}
/* oscillator o's cycle over w columns from x0, h either side of the line mid, moved dx columns (THICK's copies); not
 * built yet (a light track, the table just picked): the line alone */
static void wave_flat(const track_t *t, uint32_t o, int32_t x0, int32_t mid, int32_t w, int32_t h, uint16_t c,
                      int32_t thick, int32_t dx)
{
    const wv_part_t *P = wv_of(t);
    wv_osc_t O;
    uint32_t f0, L, step = 0xFFFFFFFFu / (uint32_t)w;
    int32_t x, py = mid;
    if (!P || t->engine != ENGI_WAVE || !(P->ready[o] & 7u)) {
        cv_rect(x0, mid, w, 1, T_LINE);
        return;
    }
    L = (P->ready[o] & 1u) ? 0u : (P->ready[o] & 2u) ? 1u : 2u;   /* the finest level that is whole */
    (void)wv_warp_set(&O, wv_kind(t->p, o), wv_amt(t->p, o));
    if (O.kind == WK_FM)                                 /* (the other oscillator's doing: not in one's own picture) */
        O.kind = WK_OFF;
    O.pf = wv_frame(wave_pos(t, P, o), &f0);
    O.sh = 24u + L;
    O.mask = (256u >> L) - 1u;
    O.a = &P->tab[o][f0][WV_OFF[L]];
    O.b = &P->tab[o][f0 + 1u][WV_OFF[L]];
    for (x = 0; x < w; x++) {
        int32_t y = mid - clamp(wv_osc(&O, (uint32_t)((x - dx + 2 * w) % w) * step, 0) * h / 17000, -h, h);
        if (x)
            cv_line_t(x0 + x - 1, py, x0 + x, y, c, thick);
        py = y;
    }
}
/* Skiff's own EDIT > OSC: A's cycle across the panel */
static void graph_wave(const track_t *t, uint16_t c, uint32_t o)
{
    pg_text(12, 16, N_WV_TABLE[wv_tab(t->p, o) % WV_NTAB], T_MID);
    cv_rect(12, 70, 216, 1, T_LINE);
    wave_flat(t, o, 12, 70, 216, 40, c, 2, 0);
}
/* the SUB's shape, two cycles, as high as its level */
static void wave_sub_flat(const track_t *t, int32_t x0, int32_t mid, int32_t w, int32_t h, uint16_t c)
{
    int32_t lv = clamp(t->p[P_E3], 0, 127), x, py = mid;
    uint32_t k = wv_subk(t->p), step = 0xFFFFFFFFu / (uint32_t)w * 2u;
    for (x = 0; x < w; x++) {
        int32_t y = mid - wv_sub_at(k, (uint32_t)x * step) * h / 32768 * (20 + lv) / 147;
        if (x)
            cv_line_t(x0 + x - 1, py, x0 + x, y, lv ? c : T_DIM, 2);
        py = y;
    }
}
/* the NOISE: what its generator gives through its COLOUR, as high as its level */
static void wave_noise_flat(const track_t *t, int32_t x0, int32_t mid, int32_t w, int32_t h, uint16_t c)
{
    int32_t lv = wv_x(t->p, WX_NLVL), nc = wv_x(t->p, WX_NCOL), coef = clamp(600 + nc * nc * 2, 0, 32767), lp = 0, x,
            py = mid, gain = 32768 + (127 - nc) * 1500;   /* (a dark one is small: shown as large as a bright one) */
    uint32_t nz = 0x2F6E2B1u;
    for (x = -40; x < w; x++) {
        int32_t y;
        nz ^= nz << 13;
        nz ^= nz >> 17;
        nz ^= nz << 5;
        lp += ((((int32_t)(int16_t)(nz >> 8)) - lp) * coef) >> 15;
        if (x < 0)
            continue;
        y = mid - clamp(((lp >> 3) * (gain >> 6)) >> 6, -32767, 32767) * h / 32768 * (20 + lv) / 147;
        if (x)
            cv_line(x0 + x - 1, py, x0 + x, y, lv ? c : T_DIM);
        py = y;
    }
}
/* the filter's level at d (the cutoff's place minus this one, / 1000 of the pitch axis), 0 .. 1000 (620: as it came
 * in), by its kind: ui_pgraph.c flt_level's LOW, BAND and HIGH; LOW 24 falling twice as fast; NOTCH a slice out */
static int32_t wave_flt_level(uint32_t kind, int32_t d, int32_t res)
{
    int32_t ad = d < 0 ? -d : d, lv;
    switch (kind) {
    case WF_BAND:
        return flt_level(d, res, 64);
    case WF_HIGH:
        return flt_level(d, res, 127);
    case WF_LOW24:
        lv = flt_level(d, res, 0);
        return d < 0 ? clamp(lv + d * 32 / 10, 0, 1000) : lv;
    case WF_NOTCH:
        return ad < 70 ? 620 * ad / 70 : 620;
    default:
        return flt_level(d, res, 0);
    }
}
/* .. its curve over w columns from x0, between the rows top and foot, the cutoff's line through it */
static void wave_flt_curve(const track_t *t, int32_t x0, int32_t top, int32_t w, int32_t foot, uint16_t c)
{
    int32_t cut = clamp(t->p[P_E4], 0, 127), res = clamp(t->p[P_E5], 0, 127), fc = cut * 1000 / 127, x, py = foot;
    uint32_t kind = wv_fkind(t->p);
    uint16_t fill = ux_mix(c, T_SURF, 75);
    cv_rect(x0 + fc * w / 1000, top, 1, foot - top, T_DIM);
    for (x = 0; x <= w; x++) {
        int32_t y = foot - wave_flt_level(kind, fc - x * 1000 / w, res) * (foot - top) / 1000;
        pg_under(x0 + x, y, top, foot, fill);
        if (x)
            cv_line_t(x0 + x - 1, py, x0 + x, y, c, 2);
        py = y;
    }
}
