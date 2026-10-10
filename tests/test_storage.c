/* test_storage.c - settings and save slot persistence (core/storage.c). */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* storage.h before <windows.h>: windows.h defines an S_OK macro that clashes with i18n.h */
#include "storage.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#include <sys/utime.h>
#else
#include <unistd.h>
#include <utime.h>
#endif

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

static char g_test_dir[1024];

/* Fresh, empty directory for the storage layer. */
static void make_test_dir(void)
{
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(sizeof tmp, tmp);
    if (n == 0 || n >= sizeof tmp) strcpy(tmp, ".\\");
    snprintf(g_test_dir, sizeof g_test_dir, "%scaro_test_%lu_%lu", tmp,
             (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
    if (_mkdir(g_test_dir) != 0) {
        fprintf(stderr, "mkdir failed\n");
        exit(1);
    }
#else
    const char *base = getenv("TMPDIR");
    snprintf(g_test_dir, sizeof g_test_dir, "%s/caro_test_XXXXXX", base && *base ? base : "/tmp");
    if (!mkdtemp(g_test_dir)) {
        fprintf(stderr, "mkdtemp failed\n");
        exit(1);
    }
#endif
    storage_set_dir(g_test_dir);
}

static void path_of(char *out, size_t cap, const char *name)
{
    snprintf(out, cap, "%s/%s", storage_dir(), name);
}

static void remove_test_files(void)
{
    char p[1200], name[32];
    int i;
    path_of(p, sizeof p, "settings.cfg"); remove(p);
    path_of(p, sizeof p, "settings.cfg.tmp"); remove(p);
    for (i = 1; i <= SAVE_SLOTS; i++) {
        snprintf(name, sizeof name, "slot%02d.sav", i);
        path_of(p, sizeof p, name); remove(p);
        snprintf(name, sizeof name, "slot%02d.sav.tmp", i);
        path_of(p, sizeof p, name); remove(p);
    }
}

/* Remove every file this test wrote, then the directory itself. */
static void cleanup_test_dir(void)
{
    remove_test_files();
#ifdef _WIN32
    _rmdir(g_test_dir);
#else
    rmdir(g_test_dir);
#endif
    storage_set_dir(NULL);
}

static void write_named(const char *name, const char *text)
{
    char p[1200];
    FILE *f;
    path_of(p, sizeof p, name);
    f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

/* Write a complete save file for `slot` with the given board, state and move list. */
static void write_slot(int slot, int board_n, int status, int reason, int winner, int turn,
                       const int mv[][2], int nmoves, long long saved_at)
{
    char name[32], p[1200];
    FILE *f;
    int i;
    snprintf(name, sizeof name, "slot%02d.sav", slot + 1);
    path_of(p, sizeof p, name);
    f = fopen(p, "wb");
    CHECK(f != NULL);
    if (!f) return;
    fprintf(f, "CARO-SAVE 2\nmode=0\ndiff=1\nboard=%d\nblock=0\n"
               "p1name=Fireboy\np1avatar=0\np2name=Watergirl\np2avatar=1\n"
               "ai=1\nturn=%d\nclock1=500\nclock2=480\nstart=1700000000\nelapsed=20\n"
               "status=%d\nreason=%d\nwinner=%d\nsaved=%lld\nmoves=%d\n",
            board_n, turn, status, reason, winner, saved_at, nmoves);
    for (i = 0; i < nmoves; i++)
        fprintf(f, "move=%d,%d\n", mv[i][0], mv[i][1]);
    fprintf(f, "end=1\n");
    fclose(f);
}

/* Read the on-disk bytes of `slot` (save files contain no NULs). Returns the length. */
static size_t read_slot_bytes(int slot, char *buf, size_t cap)
{
    char name[32], p[1200];
    FILE *f;
    size_t n;
    snprintf(name, sizeof name, "slot%02d.sav", slot + 1);
    path_of(p, sizeof p, name);
    f = fopen(p, "rb");
    if (!f) return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static void write_bytes(const char *name, const char *buf, size_t len)
{
    char p[1200];
    FILE *f;
    path_of(p, sizeof p, name);
    f = fopen(p, "wb");
    CHECK(f != NULL);
    if (f) {
        fwrite(buf, 1, len, f);
        fclose(f);
    }
}

static void make_game(Game *g, GameMode mode, bool block, int board_n, int nmoves, const int mv[][2])
{
    PlayerInfo pl[2];
    int i;
    memset(pl, 0, sizeof pl);
    strcpy(pl[0].name, "Fireboy");
    pl[0].avatar = 0;
    strcpy(pl[1].name, "Watergirl");
    pl[1].avatar = 1;
    memset(g, 0, sizeof *g);
    g->mode = mode;
    g->diff = DIFF_HARD;
    g->ai_player = mode == MODE_PVE ? 1 : -1;
    board_init(&g->board, board_n, block);
    g->players[0] = pl[0];
    g->players[1] = pl[1];
    g->clock[0] = g->clock[1] = GAME_CLOCK_SECONDS;
    g->start_time = 1700000000;
    g->winner = -1;
    for (i = 0; i < nmoves; i++) {
        Pos p = { mv[i][0], mv[i][1] };
        g->moves[i] = p;
        board_place(&g->board, p.r, p.c, i % 2 == 0 ? CELL_X : CELL_O);
    }
    g->move_count = nmoves;
    g->turn = nmoves % 2;
    g->undo_requester = -1;
}

static bool settings_equal(const Settings *a, const Settings *b)
{
    return a->lang == b->lang && a->bgm_volume == b->bgm_volume && a->sfx_volume == b->sfx_volume
        && a->bgm_muted == b->bgm_muted && a->sfx_muted == b->sfx_muted
        && a->reduced_motion == b->reduced_motion && a->fullscreen == b->fullscreen
        && memcmp(a->profile, b->profile, sizeof a->profile) == 0
        && a->last_mode == b->last_mode && a->last_board == b->last_board
        && a->last_diff == b->last_diff && a->block_rule == b->block_rule;
}

/* ---------- settings ---------- */

static void test_settings_roundtrip(void)
{
    Settings s, t;
    settings_default(&s);
    s.lang = LANG_VI;
    s.bgm_volume = 0.25f;
    s.sfx_volume = 0.5f;
    s.bgm_muted = true;
    s.sfx_muted = false;
    s.reduced_motion = true;
    s.fullscreen = true;
    strcpy(s.profile[0].name, "Thủy Tinh");
    s.profile[0].avatar = 3;
    strcpy(s.profile[1].name, "a=b%c\nd");
    s.profile[1].avatar = 5;
    s.last_mode = MODE_PVP;
    s.last_board = 5;
    s.last_diff = DIFF_HARD;
    s.block_rule = true;

    CHECK(settings_save(&s));
    memset(&t, 0x5A, sizeof t);
    CHECK(settings_load(&t));
    CHECK(settings_equal(&s, &t));
    CHECK(strcmp(t.profile[0].name, "Thủy Tinh") == 0);
    CHECK(strcmp(t.profile[1].name, "a=b%c\nd") == 0);
}

static void test_settings_missing(void)
{
    Settings d, t;
    settings_default(&d);
    memset(&t, 0x5A, sizeof t);
    CHECK(!settings_load(&t));
    CHECK(settings_equal(&d, &t));
    CHECK(d.lang == LANG_EN && d.last_board == 10 && d.last_diff == DIFF_MEDIUM);
    CHECK(strcmp(d.profile[0].name, "Fireboy") == 0 && d.profile[1].avatar == 1);
}

static void test_settings_corrupt(void)
{
    Settings d, t;
    settings_default(&d);
    write_named("settings.cfg", "garbage\nbgm_volume=0.1\n");
    memset(&t, 0x5A, sizeof t);
    CHECK(!settings_load(&t));
    CHECK(settings_equal(&d, &t));
}

static void test_settings_clamp(void)
{
    Settings d, t;
    settings_default(&d);
    write_named("settings.cfg",
                "CARO-SETTINGS 1\nbgm_volume=5\nsfx_volume=-2\nlang=9\nlast_board=7\n"
                "p1_avatar=99\np2_avatar=-3\nunknown_key=1\nlast_diff=7\nbgm_muted=yes\n"
                "p1_name=\n");
    CHECK(settings_load(&t));
    CHECK(t.bgm_volume == 1.0f);
    CHECK(t.sfx_volume == 0.0f);
    CHECK(t.lang == d.lang);
    CHECK(t.last_board == d.last_board);
    CHECK(t.profile[0].avatar == AVATAR_COUNT - 1);
    CHECK(t.profile[1].avatar == 0);
    CHECK(t.last_diff == d.last_diff);
    CHECK(t.bgm_muted == d.bgm_muted);
    CHECK(strcmp(t.profile[0].name, "Fireboy") == 0);
}

/* ---------- games ---------- */

static void test_game_roundtrip(void)
{
    static const int moves7[7][2] = { {0,0},{1,0},{0,1},{1,1},{0,2},{1,2},{0,3} };
    static const int moves9[9][2] = { {0,0},{1,0},{0,1},{1,1},{0,2},{1,2},{0,3},{1,3},{0,4} };
    static const int moves2[2][2] = { {2,2},{2,3} };
    Game g, h;

    /* in progress, PvE, blocked-rule board, awkward names and doubles */
    make_game(&g, MODE_PVE, true, 10, 7, moves7);
    strcpy(g.players[0].name, "Thủy\nTinh=%");
    g.players[0].avatar = 4;
    g.clock[0] = 500.125;
    g.clock[1] = 420.3;
    g.elapsed = 123.456789;
    g.start_time = 1700000000;
    CHECK(storage_save_game(2, &g));
    memset(&h, 0x5A, sizeof h);
    CHECK(storage_load_game(2, &h));
    CHECK(h.mode == g.mode && h.diff == g.diff && h.ai_player == g.ai_player);
    CHECK(h.board.n == 10 && h.board.block_rule && h.board.win_len == 5);
    CHECK(memcmp(h.board.c, g.board.c, sizeof g.board.c) == 0);
    CHECK(h.move_count == 7 && h.turn == g.turn);
    CHECK(memcmp(h.moves, g.moves, sizeof(Pos) * 7) == 0);
    CHECK(h.clock[0] == g.clock[0] && h.clock[1] == g.clock[1]);
    CHECK(h.elapsed == g.elapsed && h.start_time == g.start_time);
    CHECK(h.status == GS_PLAYING && h.end_reason == END_NONE && h.winner == -1);
    CHECK(strcmp(h.players[0].name, g.players[0].name) == 0 && h.players[0].avatar == 4);
    CHECK(strcmp(h.players[1].name, "Watergirl") == 0 && h.players[1].avatar == 1);
    CHECK(h.undo_requester == -1 && h.afk == AFK_NONE && h.idle == 0.0 && h.afk_left == 0.0);

    {
        SlotInfo info[SAVE_SLOTS];
        storage_list_slots(info);
        CHECK(info[2].used && info[2].move_count == 7 && info[2].board_n == 10);
        CHECK(info[2].mode == MODE_PVE && info[2].diff == DIFF_HARD);
        CHECK(strcmp(info[2].p2, "Watergirl") == 0);
        CHECK(info[2].saved_at >= (int64_t)time(NULL) - 60);
        CHECK(!info[0].used && !info[9].used);
    }

    /* PvP timeout: negative clock, no line */
    make_game(&g, MODE_PVP, false, 5, 2, moves2);
    g.ai_player = -1;
    g.status = GS_OVER;
    g.end_reason = END_TIMEOUT;
    g.winner = 1;
    g.clock[0] = -1.5;
    CHECK(storage_save_game(3, &g));
    CHECK(storage_load_game(3, &h));
    CHECK(h.mode == MODE_PVP && h.ai_player == -1 && h.board.n == 5 && !h.board.block_rule);
    CHECK(h.status == GS_OVER && h.end_reason == END_TIMEOUT && h.winner == 1);
    CHECK(h.clock[0] == -1.5 && h.clock[1] == g.clock[1]);
    CHECK(!h.win_line.found);

    /* finished by a five in a row */
    make_game(&g, MODE_PVE, false, 10, 9, moves9);
    g.status = GS_OVER;
    g.end_reason = END_LINE;
    g.winner = 0;
    g.turn = 0;
    CHECK(storage_save_game(4, &g));
    memset(&h, 0x5A, sizeof h);
    CHECK(storage_load_game(4, &h));
    CHECK(h.status == GS_OVER && h.end_reason == END_LINE && h.winner == 0);
    CHECK(h.win_line.found && h.win_line.who == CELL_X && h.win_line.len >= 5);
    CHECK(h.win_line.cells[0].r == 0 && h.win_line.cells[0].c == 0);
}

/* A complete v2 save body (everything after the magic line), two moves, valid names. */
#define V2_HEAD "CARO-SAVE 2\nmode=0\ndiff=1\nboard=10\nblock=0\n"
#define V2_TAIL \
    "p1avatar=0\np2name=Watergirl\np2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n" \
    "start=1700000000\nelapsed=20\nstatus=0\nreason=0\nwinner=-1\nsaved=1000\n" \
    "moves=2\nmove=0,0\nmove=1,0\nend=1\n"

static void test_corrupt_save(void)
{
    Game g;
    SlotInfo info[SAVE_SLOTS];
    static const int moves2[2][2] = { {0,0},{1,0} };

    /* sanity: a hand-written file is accepted */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, moves2, 2, 1000);
    CHECK(storage_load_game(0, &g));
    CHECK(g.move_count == 2);

    /* garbage */
    write_named("slot01.sav", "\x01\x02 not a save\n");
    memset(&g, 0x5A, sizeof g);
    CHECK(!storage_load_game(0, &g));
    CHECK(g.move_count == 0x5A5A5A5A); /* *g untouched on failure */

    /* header only (truncated) */
    write_named("slot01.sav", "CARO-SAVE 2\nmode=0\n");
    CHECK(!storage_load_game(0, &g));

    /* bad field value (otherwise complete) */
    write_named("slot01.sav", "CARO-SAVE 2\nmode=7\ndiff=1\nboard=10\nblock=0\np1name=A\n"
                              "p1avatar=0\np2name=B\np2avatar=1\nai=1\nturn=0\nclock1=1\n"
                              "clock2=1\nstart=1\nelapsed=0\nstatus=0\nreason=0\nwinner=-1\n"
                              "saved=1\nmoves=0\nend=1\n");
    CHECK(!storage_load_game(0, &g));

    /* a file with a bad header but otherwise valid content */
    write_named("slot01.sav", "CARO-SAVE 3\nmode=0\n");
    CHECK(!storage_load_game(0, &g));

    storage_list_slots(info);
    CHECK(!info[0].used);
    CHECK(info[0].move_count == 0 && info[0].saved_at == 0);
}

/* Format v2 is the only accepted format; v1 files (no moves/end) are treated as unused. */
static void test_save_format_v1_rejected(void)
{
    Game g;
    SlotInfo info[SAVE_SLOTS];

    write_named("slot01.sav",
                "CARO-SAVE 1\nmode=0\ndiff=1\nboard=10\nblock=0\np1name=Fireboy\np1avatar=0\n"
                "p2name=Watergirl\np2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                "start=1700000000\nelapsed=20\nstatus=0\nreason=0\nwinner=-1\nsaved=1000\n"
                "move=0,0\nmove=1,0\n");
    CHECK(!storage_load_game(0, &g));
    storage_list_slots(info);
    CHECK(!info[0].used);
    CHECK(storage_first_free_slot() == 0);
}

/* Truncation and tampering: a save is accepted only when complete and consistent. */
static void test_truncated_and_padded_saves(void)
{
    Game g;
    static const int moves2[2][2] = { {0,0},{1,0} };
    char buf[4096];
    size_t len, k;

    /* the reference body is itself valid */
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\n" V2_TAIL);
    CHECK(storage_load_game(0, &g));
    CHECK(g.move_count == 2);

    /* every proper prefix of a real file is refused (dropping only the final newline
     * still leaves a complete file, so that case is not checked) */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, moves2, 2, 1000);
    len = read_slot_bytes(0, buf, sizeof buf);
    CHECK(len > 0);
    for (k = 0; k + 1 < len; k++) {
        write_bytes("slot01.sav", buf, k);
        memset(&g, 0x5A, sizeof g);
        if (storage_load_game(0, &g)) {
            fprintf(stderr, "truncated at %lu accepted\n", (unsigned long)k);
            CHECK(0);
        }
    }
    write_bytes("slot01.sav", buf, len);
    CHECK(storage_load_game(0, &g));

    /* missing end marker, wrong end value, or anything after it */
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=Watergirl\n"
                              "p2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                              "start=1700000000\nelapsed=20\nstatus=0\nreason=0\n"
                              "winner=-1\nsaved=1000\nmoves=2\nmove=0,0\nmove=1,0\n");
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=Watergirl\n"
                              "p2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                              "start=1700000000\nelapsed=20\nstatus=0\nreason=0\n"
                              "winner=-1\nsaved=1000\nmoves=2\nmove=0,0\nmove=1,0\nend=0\n");
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=Watergirl\n"
                              "p2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                              "start=1700000000\nelapsed=20\nstatus=0\nreason=0\n"
                              "winner=-1\nsaved=1000\nmoves=2\nmove=0,0\nmove=1,0\nend=1\n"
                              "move=2,0\n");
    CHECK(!storage_load_game(0, &g));

    /* declared count differs from the move lines present (either direction) */
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=Watergirl\n"
                              "p2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                              "start=1700000000\nelapsed=20\nstatus=0\nreason=0\n"
                              "winner=-1\nsaved=1000\nmoves=3\nmove=0,0\nmove=1,0\nend=1\n");
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=Watergirl\n"
                              "p2avatar=1\nai=1\nturn=0\nclock1=500\nclock2=480\n"
                              "start=1700000000\nelapsed=20\nstatus=0\nreason=0\n"
                              "winner=-1\nsaved=1000\nmoves=1\nmove=0,0\nmove=1,0\nend=1\n");
    CHECK(!storage_load_game(0, &g));
}

/* Player names: empty is refused, and %00 (an embedded NUL) is refused. */
static void test_bad_player_names(void)
{
    Game g;

    write_named("slot01.sav", V2_HEAD "p1name=\n" V2_TAIL);
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\np1avatar=0\np2name=\np2avatar=1\n"
                              "ai=1\nturn=0\nclock1=500\nclock2=480\nstart=1700000000\n"
                              "elapsed=20\nstatus=0\nreason=0\nwinner=-1\nsaved=1000\n"
                              "moves=2\nmove=0,0\nmove=1,0\nend=1\n");
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=A%00B\n" V2_TAIL);
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fire%00\n" V2_TAIL);
    CHECK(!storage_load_game(0, &g));
    write_named("slot01.sav", V2_HEAD "p1name=Fireboy\n" V2_TAIL);
    CHECK(storage_load_game(0, &g));
    CHECK(strcmp(g.players[0].name, "Fireboy") == 0);
    write_named("slot01.sav", V2_HEAD "p1name=Fire%20boy\n" V2_TAIL);
    CHECK(storage_load_game(0, &g));
    CHECK(strcmp(g.players[0].name, "Fire boy") == 0);
}

static void test_illegal_moves(void)
{
    static const int dup[2][2]   = { {0,0},{0,0} };
    static const int oob[2][2]   = { {0,0},{10,0} };
    static const int win_mid[10][2] = { {0,0},{1,0},{0,1},{1,1},{0,2},{1,2},{0,3},{1,3},{0,4},{2,0} };
    static const int ok[2][2]    = { {0,0},{1,0} };
    static const int five[9][2]  = { {0,0},{1,0},{0,1},{1,1},{0,2},{1,2},{0,3},{1,3},{0,4} };
    Game g;
    int i;

    /* valid control */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 1000);
    CHECK(storage_load_game(0, &g));

    /* duplicate cell */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, dup, 2, 1000);
    CHECK(!storage_load_game(0, &g));
    /* out of bounds */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, oob, 2, 1000);
    CHECK(!storage_load_game(0, &g));
    /* the same two moves are fine on a 3x3 board */
    write_slot(0, 3, GS_PLAYING, END_NONE, -1, 0, ok, 2, 1000);
    CHECK(storage_load_game(0, &g));
    /* five-in-a-row moves do not fit a 3x3 board */
    write_slot(0, 3, GS_PLAYING, END_NONE, -1, 0, five, 9, 1000);
    CHECK(!storage_load_game(0, &g));
    /* play continues after a five in a row */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, win_mid, 10, 1000);
    CHECK(!storage_load_game(0, &g));
    /* wrong player to move */
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 1, ok, 2, 1000);
    CHECK(!storage_load_game(0, &g));
    /* status says line win but the last move does not win */
    write_slot(0, 10, GS_OVER, END_LINE, 0, 0, ok, 2, 1000);
    CHECK(!storage_load_game(0, &g));
    /* status says draw on a board that is not full */
    write_slot(0, 10, GS_OVER, END_DRAW, -1, 0, ok, 2, 1000);
    CHECK(!storage_load_game(0, &g));
    /* move count beyond the board */
    {
        int many[GAME_MAX_MOVES + 1][2];
        for (i = 0; i <= GAME_MAX_MOVES; i++) { many[i][0] = i / CARO_MAX_N; many[i][1] = i % CARO_MAX_N; }
        write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, many, GAME_MAX_MOVES + 1, 1000);
        CHECK(!storage_load_game(0, &g));
    }

    /* the same illegal list is refused on save as well */
    {
        Game bad;
        make_game(&bad, MODE_PVE, false, 10, 2, ok);
        bad.moves[1].r = 0; bad.moves[1].c = 0; /* duplicate of move 0 */
        CHECK(!storage_save_game(1, &bad));
        CHECK(!storage_load_game(1, &g));
    }
}

static void test_slot_selection(void)
{
    static const int ok[2][2] = { {0,0},{1,0} };
    SlotInfo info[SAVE_SLOTS];
    Game g;
    int i;

    /* empty */
    CHECK(storage_first_free_slot() == 0);
    CHECK(storage_autosave_slot() == 0);
    CHECK(storage_latest_slot() == -1);

    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 100);
    write_slot(1, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 300);
    write_slot(2, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 200);
    write_slot(3, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 50);
    CHECK(storage_first_free_slot() == 4);
    CHECK(storage_autosave_slot() == 4);
    CHECK(storage_latest_slot() == 1);

    /* all used: autosave overwrites the oldest (slot 3, saved_at 50) */
    for (i = 4; i < SAVE_SLOTS; i++)
        write_slot(i, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 1000 + i);
    storage_list_slots(info);
    for (i = 0; i < SAVE_SLOTS; i++) CHECK(info[i].used);
    CHECK(storage_first_free_slot() == -1);
    CHECK(storage_autosave_slot() == 3);
    CHECK(storage_latest_slot() == 9);

    /* delete frees the slot; deleting twice is fine */
    CHECK(storage_delete_slot(3));
    CHECK(storage_delete_slot(3));
    storage_list_slots(info);
    CHECK(!info[3].used);
    CHECK(storage_first_free_slot() == 3);
    CHECK(storage_autosave_slot() == 3);
    CHECK(storage_latest_slot() == 9);

    /* corrupt slot counts as unused */
    write_named("slot01.sav", "junk");
    CHECK(storage_first_free_slot() == 0);
    CHECK(storage_autosave_slot() == 0);

    /* a real save stamps the current time, so it becomes the latest */
    make_game(&g, MODE_PVE, false, 10, 2, ok);
    CHECK(storage_save_game(0, &g));
    CHECK(storage_latest_slot() == 0);
    CHECK(storage_first_free_slot() == 3);
    CHECK(storage_autosave_slot() == 3);

    /* out-of-range slots */
    CHECK(!storage_save_game(-1, &g));
    CHECK(!storage_save_game(SAVE_SLOTS, &g));
    CHECK(!storage_load_game(SAVE_SLOTS, &g));
    CHECK(!storage_delete_slot(-1));
}

/* Saves made within the same second must still order strictly (latest / autosave). */
static void test_save_ordering(void)
{
    static const int ok[2][2] = { {0,0},{1,0} };
    SlotInfo info[SAVE_SLOTS];
    Game g;
    int64_t prev = 0;
    int i;

    make_game(&g, MODE_PVE, false, 10, 2, ok);
    for (i = 0; i < 5; i++) CHECK(storage_save_game(i, &g));
    storage_list_slots(info);
    for (i = 0; i < 5; i++) {
        CHECK(info[i].used);
        CHECK(info[i].saved_at > prev);
        prev = info[i].saved_at;
    }
    CHECK(storage_latest_slot() == 4);

    /* re-saving an older slot makes it the latest, even within the same second */
    CHECK(storage_save_game(1, &g));
    CHECK(storage_latest_slot() == 1);

    /* all slots full: autosave overwrites the oldest, which is slot 0 */
    for (i = 5; i < SAVE_SLOTS; i++) CHECK(storage_save_game(i, &g));
    CHECK(storage_first_free_slot() == -1);
    CHECK(storage_autosave_slot() == 0);
    CHECK(storage_latest_slot() == SAVE_SLOTS - 1);
}

#ifndef _WIN32
static void set_mtime(int slot, time_t t)
{
    char name[32], p[1200];
    struct utimbuf ub;
    snprintf(name, sizeof name, "slot%02d.sav", slot + 1);
    path_of(p, sizeof p, name);
    ub.actime = t;
    ub.modtime = t;
    CHECK(utime(p, &ub) == 0);
}
#endif

/* Equal saved_at (hand-made files): the most recently written file wins; equal mtimes
 * fall back to the lower slot index. */
static void test_latest_tie_break(void)
{
#ifndef _WIN32
    static const int ok[2][2] = { {0,0},{1,0} };
    write_slot(0, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 500);
    write_slot(1, 10, GS_PLAYING, END_NONE, -1, 0, ok, 2, 500);
    set_mtime(0, 1000);
    set_mtime(1, 2000);
    CHECK(storage_latest_slot() == 1);
    set_mtime(0, 3000);
    CHECK(storage_latest_slot() == 0);
    set_mtime(0, 2000);
    CHECK(storage_latest_slot() == 0);
#endif
}

/* Saving during the AFK prompt or with an undo request pending is allowed; the loaded game
 * has AFK reset and no undo pending. */
static void test_save_during_afk_and_undo(void)
{
    static const int moves2[2][2] = { {0,0},{1,0} };
    Game g, h;

    make_game(&g, MODE_PVP, false, 10, 2, moves2);
    g.afk = AFK_PROMPT;
    g.afk_left = 7.0;
    g.idle = 130.0;
    g.undo_requester = 1;
    CHECK(storage_save_game(5, &g));
    memset(&h, 0x5A, sizeof h);
    CHECK(storage_load_game(5, &h));
    CHECK(h.afk == AFK_NONE && h.afk_left == 0.0 && h.idle == 0.0);
    CHECK(h.undo_requester == -1);
    CHECK(h.move_count == 2 && h.turn == g.turn && h.status == GS_PLAYING);

    /* the AFK timeout state is also saved and comes back as a normal, resumable game */
    make_game(&g, MODE_PVE, false, 10, 2, moves2);
    g.afk = AFK_TIMEOUT;
    g.afk_left = 0.0;
    g.idle = 200.0;
    CHECK(storage_save_game(6, &g));
    memset(&h, 0x5A, sizeof h);
    CHECK(storage_load_game(6, &h));
    CHECK(h.afk == AFK_NONE && h.idle == 0.0 && h.undo_requester == -1);
    CHECK(h.status == GS_PLAYING && h.move_count == 2);
}

static void test_storage_dir(void)
{
    storage_set_dir("/some/where");
    CHECK(strcmp(storage_dir(), "/some/where") == 0);
    storage_set_dir(NULL);
    CHECK(storage_dir() != NULL && storage_dir()[0] != '\0');
    storage_set_dir(g_test_dir);
}

/* An over-long CARO_DATA_DIR is ignored: the OS default is used, never the CWD ("caro"). */
static void test_long_data_dir_env(void)
{
#ifndef _WIN32
    static char longdir[2100];
    size_t i;
    longdir[0] = '/';
    for (i = 1; i < 2000; i++) longdir[i] = 'x';
    longdir[2000] = '\0';
    storage_set_dir(NULL);
    setenv("CARO_DATA_DIR", longdir, 1);
    CHECK(strcmp(storage_dir(), longdir) != 0);
    CHECK(strcmp(storage_dir(), "caro") != 0);
    CHECK(strlen(storage_dir()) < 1000);
    unsetenv("CARO_DATA_DIR");
    storage_set_dir(g_test_dir);
#endif
}

int main(void)
{
    make_test_dir();

    remove_test_files();
    test_settings_roundtrip();
    remove_test_files();
    test_settings_missing();
    remove_test_files();
    test_settings_corrupt();
    remove_test_files();
    test_settings_clamp();
    remove_test_files();
    test_game_roundtrip();
    remove_test_files();
    test_corrupt_save();
    remove_test_files();
    test_illegal_moves();
    remove_test_files();
    test_slot_selection();
    remove_test_files();
    test_save_format_v1_rejected();
    remove_test_files();
    test_truncated_and_padded_saves();
    remove_test_files();
    test_bad_player_names();
    remove_test_files();
    test_save_ordering();
    remove_test_files();
    test_latest_tie_break();
    remove_test_files();
    test_save_during_afk_and_undo();
    remove_test_files();
    test_storage_dir();
    test_long_data_dir_env();

    cleanup_test_dir();
    if (g_failures) {
        fprintf(stderr, "test_storage: %d check(s) failed\n", g_failures);
        return 1;
    }
    puts("test_storage: all checks passed");
    return 0;
}
