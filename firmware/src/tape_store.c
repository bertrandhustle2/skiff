/* SPDX-License-Identifier: GPL-3.0-only
 * The tape kept in flash (tape.c): one with each of the four projects (project.c: SAVE writes it, LOAD brings it
 * back, DELETE drops it) and one with the autosave (the tape as it was when the unit was switched off).
 *
 * Where: the third user-sample slot's twenty sectors, 0xC8000 .. 0xDBFFF (eng_sample.c: the FM-1 has two user slots
 * now). A tape is one or two chunks of up to TS_PER notes; a chunk has two sectors, A and B, each written as
 * storage.c writes its objects: erased, the payload from offset 256, the commit record at 0 last, read back.
 * Every chunk carries its save's id. A save writes the second chunk first, into the sector that does not hold the
 * second chunk of the save in charge, then the first chunk (which says how many notes there are) into the sector that
 * is not the first chunk in charge. The save in charge is the newest valid first chunk; its second chunk is the copy
 * with its id. So a write torn anywhere leaves the save before whole and in charge.
 * The notes are written in the order of time, the live and finished ones only, without touching the tape (a wipe that
 * can still be undone stays so). Only with the transport stopped and nothing armed: an erase stops the audio for its
 * ~40 ms, as every save does.
 * The SMK-37 has no room set aside: no tape is kept there. Without the flash driver (the host's UI tests) the copies
 * are kept in RAM. Included by project.c (its runtime part). */
#ifndef ST_PAYLOAD_MAX                                   /* (a build with no storage.c: its sizes) */
#define ST_PAYLOAD_MAX 3840u
#define ST_SECTOR 4096u
#endif
#define TS_SLOTS 5u                                      /* the four projects', the autosave's */
#define TS_AUTO 4u
#define TS_BASE 0xC8000u
#define TS_HMAGIC 0x45504154u                            /* "TAPE": a chunk's commit record */
#define TS_PER ((ST_PAYLOAD_MAX - 16u) / 8u)             /* notes a chunk: 478 */
typedef struct {
    uint32_t id;                                         /* the save's: the same in both chunks */
    uint16_t n;                                          /* the tape's notes, both chunks' */
    uint8_t part, loop;
    uint32_t len;                                        /* the tape's end */
    uint32_t rsv;
} ts_part_t;
_Static_assert(sizeof(ts_part_t) == 16u && sizeof(tape_note_t) == 8u && TAPE_NOTES <= 2u * TS_PER, "a tape fits two chunks");
_Static_assert(TS_SLOTS * 4u * ST_SECTOR <= 0x14000u, "the tapes fit the third user-sample slot");

/* ---- a chunk's two copies. ts_read: the copy's payload into st_buf when its commit record and CRC hold, its length
 * (16 or more) and *seq; -1 none. ts_write: st_buf's first len bytes as that copy; 0 written and read back */
#if FELUCCA_FLASH && !BOARD_SMK37
static uint32_t ts_off(uint32_t chunk, uint32_t copy) { return TS_BASE + (chunk * 2u + copy) * ST_SECTOR; }
static int ts_read(uint32_t chunk, uint32_t copy, uint32_t *seq)
{
    st_hdr_t h;
    if (!flash_ok || st_read(ts_off(chunk, copy), &h, sizeof h) || h.magic != TS_HMAGIC || h.type != chunk ||
        h.slot != copy || h.len < 16u || h.len > ST_PAYLOAD_MAX || h.hcrc != st_crc32(&h, sizeof h - 4u))
        return -1;
    if (st_read(ts_off(chunk, copy) + ST_PAYLOAD_OFF, st_buf, h.len) || st_crc32(st_buf, h.len) != h.crc)
        return -1;
    *seq = h.seq;
    return (int)h.len;
}
static int ts_write(uint32_t chunk, uint32_t copy, uint32_t len, uint32_t seq)
{
    st_hdr_t h;
    uint32_t off, base = ts_off(chunk, copy), got = 0;
    int rc;
    if (!flash_ok)
        return -1;
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u)
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, len - off > 256u ? 256u : len - off)) != 0)
            return rc;
    h.magic = TS_HMAGIC;
    h.type = (uint16_t)chunk;
    h.slot = (uint16_t)copy;
    h.seq = seq;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)        /* the commit record, last */
        return rc;
    return ts_read(chunk, copy, &got) == (int)len && got == seq ? 0 : -7;   /* (read back: a failing part says so) */
}
#elif !FELUCCA_FLASH
static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4)));   /* (storage.c's, which this build has none of) */
static uint8_t ts_ram[TS_SLOTS * 2u][2][ST_PAYLOAD_MAX];
static uint16_t ts_ram_len[TS_SLOTS * 2u][2];
static uint32_t ts_ram_seq[TS_SLOTS * 2u][2];
static int ts_read(uint32_t chunk, uint32_t copy, uint32_t *seq)
{
    if (ts_ram_len[chunk][copy] < 16u)
        return -1;
    memcpy(st_buf, ts_ram[chunk][copy], ts_ram_len[chunk][copy]);
    *seq = ts_ram_seq[chunk][copy];
    return ts_ram_len[chunk][copy];
}
static int ts_write(uint32_t chunk, uint32_t copy, uint32_t len, uint32_t seq)
{
    memcpy(ts_ram[chunk][copy], st_buf, len);
    ts_ram_len[chunk][copy] = (uint16_t)len;
    ts_ram_seq[chunk][copy] = seq;
    return 0;
}
#else
static int ts_read(uint32_t chunk, uint32_t copy, uint32_t *seq) { (void)chunk; (void)copy; (void)seq; return -1; }
static int ts_write(uint32_t chunk, uint32_t copy, uint32_t len, uint32_t seq)
{
    (void)chunk; (void)copy; (void)len; (void)seq;
    return -1;
}
#endif

static int ts_newer(uint32_t a, uint32_t b) { return a != b && a - b < 0x80000000u; }   /* sequence a after b */
/* a chunk's newest valid copy (by_id: the newest holding save id's part), its payload in st_buf: its length, -1 none.
 * *copy which one, *seq its sequence */
static int ts_pick(uint32_t chunk, int by_id, uint32_t id, uint32_t *copy, uint32_t *seq)
{
    uint32_t c, s = 0, best = 2u, bs = 0;
    for (c = 0; c < 2u; c++) {
        ts_part_t p;
        if (ts_read(chunk, c, &s) < 16)
            continue;
        memcpy(&p, st_buf, sizeof p);
        if (by_id && p.id != id)
            continue;
        if (best == 2u || ts_newer(s, bs)) {
            best = c;
            bs = s;
        }
    }
    if (best == 2u)
        return -1;
    *copy = best;
    *seq = bs;
    return ts_read(chunk, best, &s);                    /* (st_buf may hold the other copy's by now: this one's) */
}

/* the tape's live, finished notes in the order of time, one at a time (its runs are each in that order): 0 at the end */
static uint16_t ts_cur[TAPE_RUNS] __attribute__((section(".pool")));
static const tape_note_t *ts_next(void)
{
    const tape_note_t *best = 0;
    uint32_t r, br = 0;
    for (r = 0; r < tape.nrun; r++) {
        const tape_run_t *q = &tape_rn[r];
        const tape_note_t *e;
        while (ts_cur[r] < q->n && !(tape_live(e = &tape_nt[(q->first + ts_cur[r]) % TAPE_NOTES]) && e->len))
            ts_cur[r]++;
        if (ts_cur[r] >= q->n)
            continue;
        e = &tape_nt[(q->first + ts_cur[r]) % TAPE_NOTES];
        if (!best || tape_st_of(e) < tape_st_of(best)) {
            best = e;
            br = r;
        }
    }
    if (best)
        ts_cur[br]++;
    return best;
}
static uint32_t ts_live(void)                            /* the notes a save keeps */
{
    uint32_t i, n = 0;
    for (i = 0; i < tape.n; i++)
        n += (uint32_t)(tape_live(&tape_nt[i]) && tape_nt[i].len);
    return n;
}
/* what the stored tape would be now: its notes (in any order: tidying the pool changes nothing), its end, LOOP. The
 * autosave writes the tape when this changes, and a project counts as changed with it (project.c autosave_sig) */
static uint32_t tape_store_sig(void)
{
    uint32_t i, h = 0, n = tape.n < TAPE_NOTES ? tape.n : TAPE_NOTES;
    for (i = 0; i < n; i++) {
        const tape_note_t *e = &tape_nt[i];
        if (tape_live(e) && e->len)
            h += (e->st * 2654435761u ^ ((uint32_t)e->len << 16 | (uint32_t)e->note << 8 | e->vel)) * 2246822519u + 1u;
    }
    return h + tape.len * 31u + (uint32_t)tape.loop * 7u;
}
static int tape_store_quiet(void)                        /* stopped, nothing armed, nothing asked of the tape */
{
    return !(song.playing || tape.run || tape.rec || tape.busy || tape.q_arm || tape.q_disarm || tape.q_undo ||
             tape.q_clr || tape.q_retake);
}

/* the tape as it is into slot (0 .. 3 a project's, TS_AUTO the autosave's). 0: kept. Only when tape_store_quiet */
static int tape_store_save(uint32_t slot)
{
    static uint32_t ts_id __attribute__((section(".pool")));
    ts_part_t p, q[2];
    uint32_t n, i, k, part, parts, id, c1 = 2u, s1 = 0, s2[2] = {0, 0}, cur_id = 0, to, seq;
    int two = 0, v2[2];
    uint8_t gen0 = tape.gen;
    uint16_t n0 = tape.n;
    if (slot >= TS_SLOTS || !tape_store_quiet())
        return -1;
    n = ts_live();
    parts = n > TS_PER ? 2u : 1u;
    if (ts_pick(slot * 2u, 0, 0, &c1, &s1) >= 16) {     /* the save in charge: its first chunk's copy, its id */
        memcpy(&p, st_buf, sizeof p);
        two = p.n > TS_PER;
        cur_id = p.id;
    } else {
        c1 = 2u;
    }
    memset(q, 0, sizeof q);
    for (i = 0; i < 2u; i++) {                          /* the second chunk's copies: whose they are */
        v2[i] = ts_read(slot * 2u + 1u, i, &s2[i]) >= 16;
        if (v2[i])
            memcpy(&q[i], st_buf, sizeof p);
    }
    id = (ts_id += 2654435761u) ^ fm1_ms ^ n << 20;
    while ((c1 != 2u && id == cur_id) || (v2[0] && id == q[0].id) || (v2[1] && id == q[1].id))
        id += 0x9E3779B9u;
    for (part = parts; part-- > 0u;) {                  /* the second chunk first: the first one is the commit */
        uint32_t first = part * TS_PER, cnt = n - first > TS_PER ? TS_PER : n - first;
        if (part) {
            if (two && v2[0] && q[0].id == cur_id)
                to = 1u;                                /* (never over the one the save in charge needs) */
            else if (two && v2[1] && q[1].id == cur_id)
                to = 0u;
            else
                to = !v2[0] ? 0u : !v2[1] ? 1u : ts_newer(s2[0], s2[1]) ? 1u : 0u;   /* (the empty one, or the older) */
            seq = 1u + (v2[0] && (!v2[1] || !ts_newer(s2[1], s2[0])) ? s2[0] : v2[1] ? s2[1] : 0u);
        } else {
            to = c1 == 0u ? 1u : 0u;
            seq = c1 == 2u ? 1u : s1 + 1u;
        }
        for (i = 0; i < TAPE_RUNS; i++)
            ts_cur[i] = 0;
        for (k = 0; k < first; k++)
            (void)ts_next();
        p.id = id;
        p.n = (uint16_t)n;
        p.part = (uint8_t)part;
        p.loop = tape.loop;
        p.len = tape.len;
        p.rsv = 0;
        memcpy(st_buf, &p, sizeof p);
        for (k = 0; k < cnt; k++) {
            const tape_note_t *e = ts_next();
            if (!e)
                return -3;                              /* (the tape changed under it) */
            memcpy(st_buf + 16u + k * 8u, e, 8u);
        }
        if (tape.gen != gen0 || tape.n != n0 || !tape_store_quiet())
            return -3;                                  /* (a take began meanwhile: the save before stays) */
        if (ts_write(slot * 2u + part, to, 16u + cnt * 8u, seq))
            return -2;
    }
    return 0;
}
/* slot's tape becomes the tape. 0: loaded; 1: none kept there, the tape is empty now. For a stopped transport */
static int tape_store_load(uint32_t slot)
{
    ts_part_t h, h2;
    uint32_t n, cnt, i, got, c, s;
    int len;
    tape_reset();
    if (slot >= TS_SLOTS || (len = ts_pick(slot * 2u, 0, 0, &c, &s)) < 16)
        return 1;
    memcpy(&h, st_buf, sizeof h);
    n = h.n;
    cnt = n > TS_PER ? TS_PER : n;
    if (h.part != 0u || n > TAPE_NOTES || (uint32_t)len != 16u + cnt * 8u)
        return 1;
    tape.busy = 1;
    memcpy(tape_nt, st_buf + 16u, cnt * 8u);
    got = cnt;
    if (n > TS_PER) {
        memset(&h2, 0, sizeof h2);
        len = ts_pick(slot * 2u + 1u, 1, h.id, &c, &s);
        if (len >= 16)
            memcpy(&h2, st_buf, sizeof h2);
        if (len == (int)(16u + (n - TS_PER) * 8u) && h2.part == 1u && h2.n == h.n) {
            memcpy(&tape_nt[TS_PER], st_buf + 16u, (n - TS_PER) * 8u);
            got = n;
        } else {
            got = 0;                                    /* (no second half of this save: no tape) */
        }
    }
    for (i = 0; i < got; i++)                            /* (as kept: live, finished, a track's) */
        if (!tape_nt[i].len || !tape_live(&tape_nt[i]) || tape_trk_of(&tape_nt[i]) >= NTRK)
            got = 0;
    tape.n = (uint16_t)got;
    tape.nrun = got ? 1u : 0u;
    tape_rn[0].first = 0;
    tape_rn[0].n = (uint16_t)got;
    tape_rn[0].cur = 0;
    tape.len = got ? h.len : 0u;
    tape.loop = (uint8_t)(got && h.loop);
    tape.gen++;
    tape.busy = 0;
    return got ? 0 : 1;
}
/* slot's tape gone (its project deleted) */
static void tape_store_drop(uint32_t slot)
{
    ts_part_t p;
    uint32_t c, s;
    if (slot >= TS_SLOTS || ts_pick(slot * 2u, 0, 0, &c, &s) < 16)
        return;
    memcpy(&p, st_buf, sizeof p);
    if (!p.n)
        return;
    memset(&p, 0, sizeof p);
    p.id = 1u;
    memcpy(st_buf, &p, sizeof p);
    (void)ts_write(slot * 2u, c ? 0u : 1u, 16u, s + 1u);
}
