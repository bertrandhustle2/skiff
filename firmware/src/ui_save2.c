/* SPDX-License-Identifier: GPL-3.0-only
 * SAVE in the new layout (tape.c UI_NEW): two pages that each say what they keep, then the list of every sound.
 *   PROJECT  a slot (A .. D, KNOB 1) holds everything: the six sounds and their settings, the tape, the steps, the
 *            song. OCT+ saves the whole project into it (the NAME screen first; over a used one: asked), OCT- loads it.
 *            The row after D, NEW: OCT- starts an empty project (asked first when this one has changed).
 *   SOUND    one track's sound as it is now (KNOB 1 or ALGORITHM: the track) into one of the 32 slots (KNOB 2), to
 *            use in any project. OCT+ saves it there (NAME first; over a used one: asked); OCT- loads the slot's
 *            sound onto the selected track (one undo: SAVE held).
 *   PRESETS  Skiff's list of every sound, as it was.
 * The acts themselves are Skiff's (project.c, upreset.c, ui_name.c); its own PROJECT and USER pages, with their
 * knob-picked actions, are not shown in this layout, and its PATTERNS page is under SEQ (ui.c page_fam).
 * Included by ui_graph.c (after ui_pgraph.c). */
static void name_open(uint32_t kind, uint32_t slot);    /* (ui_name.c) */
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint32_t vr,
                        int32_t ratio, uint32_t icon);   /* (ui_draw.c) */
static int save2_page(void) { return !ui.home && (cur_page()->graph == GR_PSAVE || cur_page()->graph == GR_SSAVE); }

static uint32_t save2_sig(void)
{
    uint32_t h = 2166136261u ^ (uint32_t)psave_row * 7919u ^ (uint32_t)ui.uslot * 104729u ^ song.sel * 131u ^
                 up_gen * 977u ^ (uint32_t)cur_page()->graph * 31u, k;
    char b[20];
    for (k = 0; k < NTRK; k++) {
        sound_name(&trk[k], b);
        h = str_hash(h, b) + trk[k].eng_req;
    }
    for (k = 0; k < 4u; k++)
        h ^= (uint32_t)graph_project_used(k) << (20u + k);
    return h + graph_pname_sig;
}
/* a row of a list from x, w wide: its tag, its name; the picked one a band */
static void s2_row(int32_t x, int32_t y, int32_t w, int sel, const char *tag, uint16_t tc, const char *name, uint16_t nc, int32_t nx)
{
    uint16_t bg = sel ? T_THEME : T_SURF;
    if (sel)
        cv_rect(x, y, w, 14, T_THEME);
    cv_text_on(x + 4, y + CAP_IN(S, 14), &AF_S, tag, sel ? T_INK : tc, bg);
    cv_free_text(x + nx, y + CAP_IN(S, 14), &AF_S, name, sel ? T_INK : nc, bg, w - nx - 3);
}
static void graph_psave(void)
{
    uint32_t i, r = psave_row % 5u;
    char s[40], b[2] = {(char)('A' + r % 4u), 0};
    int used = r < 4u && graph_project_used(r);
    pg_text(10, 14, "SAVE THE WHOLE PROJECT", T_TEXT);
    pg_text(10, 27, "6 SOUNDS  TAPE  STEPS  SETTINGS", T_MID);
    for (i = 0; i < 4u; i++) {
        char l[2] = {(char)('A' + i), 0};
        int u = graph_project_used(i);
        const char *n = graph_project_name(i);
        s2_row(8, 32 + (int32_t)i * 15, 224, i == r, l, T_MID, !u ? "EMPTY" : n[0] ? n : "USED", u ? T_TEXT : T_DIM, 22);
    }
    s2_row(8, 32 + 4 * 15, 224, r == 4u, "+", T_MID, "NEW: AN EMPTY PROJECT", T_MID, 22);
    if (r == 4u) {
        pg_text(10, 118, "OCT+  PICK A SLOT, A TO D", T_DIM);
        pg_text(10, 131, "OCT-  START AN EMPTY PROJECT", T_TEXT);
        return;
    }
    str_cpy(s, "OCT+  SAVE EVERYTHING IN ", sizeof s);
    str_cpy(s + str_len(s), b, 2);
    pg_text(10, 118, s, T_TEXT);
    str_cpy(s, used ? "OCT-  LOAD " : "OCT-  ", sizeof s);
    str_cpy(s + str_len(s), b, 2);
    str_cpy(s + str_len(s), used ? " (REPLACES ALL THIS)" : " IS EMPTY", sizeof s - str_len(s));
    pg_text(10, 131, s, used ? T_MID : T_DIM);
}
static void graph_ssave(void)
{
    int32_t row, first = clamp((int32_t)ui.uslot - 2, 0, UP_SLOTS - 6);
    uint32_t k = song.sel % NTRK;
    char s[48], nm[20], tag[4], d[2] = {(char)('1' + k), 0};
    int used = up_used(ui.uslot);
    pg_text(10, 12, "ON THE TRACKS", T_MID);
    pg_text(126, 12, "SAVED SOUNDS", T_MID);
    for (row = 0; row < (int32_t)NTRK; row++) {
        char t[2] = {(char)('1' + row), 0};
        sound_name(&trk[row], nm);
        s2_row(8, 16 + row * 15, 110, (uint32_t)row == k, t, T_TRK((uint32_t)row), nm[0] ? nm : "--", T_TEXT, 16);
    }
    for (row = 0; row < 6; row++) {
        uint32_t u = (uint32_t)(first + row);
        up_slot_label(tag, u);
        if (up_used(u))
            up_name(u, nm);
        else
            str_cpy(nm, "--", sizeof nm);
        s2_row(122, 16 + row * 15, 110, u == ui.uslot, tag, T_MID, nm, up_used(u) ? T_TEXT : T_DIM, 30);
    }
    up_slot_label(tag, ui.uslot);
    sound_name(&trk[k], nm);
    str_cpy(s, "OCT+  SAVE ", sizeof s);                 /* the two acts, spelt out */
    str_cpy(s + str_len(s), d, 2);
    str_cpy(s + str_len(s), " ", 2);
    str_cpy(s + str_len(s), nm[0] ? nm : "--", sizeof s - str_len(s));
    str_cpy(s + str_len(s), " AS ", sizeof s - str_len(s));
    str_cpy(s + str_len(s), tag, sizeof s - str_len(s));
    pg_text(10, 118, s, T_TEXT);
    str_cpy(s, "OCT-  ", sizeof s);
    if (used) {
        up_name(ui.uslot, nm);
        str_cpy(s + str_len(s), "LOAD ", sizeof s - str_len(s));
        str_cpy(s + str_len(s), nm, sizeof s - str_len(s));
        str_cpy(s + str_len(s), " ONTO TRACK ", sizeof s - str_len(s));
        str_cpy(s + str_len(s), d, sizeof s - str_len(s));
    } else {
        str_cpy(s + str_len(s), tag, sizeof s - str_len(s));
        str_cpy(s + str_len(s), " IS EMPTY", sizeof s - str_len(s));
    }
    pg_text(10, 131, s, used ? T_MID : T_DIM);
}
/* their cards (ui_draw.c): PROJECT's SLOT; SOUND's TRACK and SLOT */
static void save2_columns(void)
{
    char v[8];
    if (cur_page()->graph == GR_PSAVE) {
        v[0] = (char)('A' + psave_row % 4u);
        v[1] = 0;
        draw_column(0, "SLOT", psave_row >= 4u ? "NEW" : v, "", VR_LIVE, (int32_t)psave_row * 1000 / 4, ICON_NONE);
        draw_column(1, "", "", "", VR_THEME, -1, ICON_NONE);
    } else {
        fmt_int(v, (int32_t)song.sel + 1);
        draw_column(0, "TRACK", v, "", VR_LIVE, (int32_t)song.sel * 1000 / (NTRK - 1), ICON_NONE);
        up_slot_label(v, ui.uslot);
        draw_column(1, "SLOT", v, "", VR_LIVE, (int32_t)ui.uslot * 1000 / (UP_SLOTS - 1), ICON_NONE);
    }
    draw_column(2, "", "", "", VR_THEME, -1, ICON_NONE);
    draw_column(3, "", "", "", VR_THEME, -1, ICON_NONE);
}
/* their knobs (ui_input.c edit_param) */
static void save2_knob(uint32_t slot, int32_t steps)
{
    if (cur_page()->graph == GR_PSAVE) {
        if (slot == 0u)
            psave_row = (uint8_t)clamp((int32_t)psave_row + steps, 0, 4);
    } else if (slot == 0u) {
        track_select((uint32_t)clamp((int32_t)song.sel + steps, 0, NTRK - 1));
    } else if (slot == 1u) {
        ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
    }
    ui.force = 1;
}
/* OCT+ (bit 1): save; OCT- (bit 0): load */
static void save2_oct(uint32_t oct)
{
    uint32_t k = psave_row & 3u;
    if (cur_page()->graph == GR_PSAVE) {
        if (psave_row >= 4u) {                           /* NEW: OCT- an empty project (asked first over changed music) */
            if (oct & 2u)
                ui_message("PICK A SLOT: A TO D");
            else if (project_changed())
                confirm_open(CF_NEW_PROJ, 0);
            else
                project_new();
            return;
        }
        if (oct & 2u) {
            if (tape_rec_on() && !transport_busy())      /* (armed, no take yet: off, so the tape is still to save) */
                tape_rec_toggle();
            if (transport_busy())
                ui_message("STOP TO SAVE");
            else if (project_used(k))
                confirm_open(CF_OVR_PROJ, k);
            else
                name_open(3u /* NK_PROJ_SAVE */, k);
        } else if (!project_used(k)) {
            ui_message("EMPTY SLOT");
        } else if (project_changed()) {
            confirm_open(CF_LOAD_PROJ, k);
        } else {
            project_load(k);
        }
        return;
    }
    if (!(oct & 2u)) {
        up_ui(0, ui.uslot);                              /* LOAD onto the selected track (says why not) */
    } else if (transport_busy()) {
        ui_message("STOP TO SAVE");
    } else if (up_used(ui.uslot) || up_old(ui.uslot)) {
        confirm_open(CF_OVR_USER, ui.uslot);
    } else {
        name_open(1u /* NK_USER_SAVE */, ui.uslot);
    }
}
