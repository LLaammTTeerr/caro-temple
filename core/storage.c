/* storage.c - settings, player profiles and 10 manual save slots as small text files.
 * Files: <dir>/settings.cfg and <dir>/slot01.sav .. slot10.sav. Each starts with a magic
 * header line, followed by key=value lines. Unknown keys are ignored. Writes are atomic
 * (write <file>.tmp, then rename over the target). */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#define CARO_MKDIR(p) _mkdir(p)
#define IS_SEP(c) ((c) == '/' || (c) == '\\')
#else
#include <sys/stat.h>
#define CARO_MKDIR(p) mkdir((p), 0755)
#define IS_SEP(c) ((c) == '/')
#endif
#include "storage.h"

#define DIR_CAP 1024
#define PATH_CAP 1200
#define LINE_CAP 1024
#define SETTINGS_MAGIC "CARO-SETTINGS 1"
#define SAVE_MAGIC "CARO-SAVE 1"
#define SETTINGS_FILE "settings.cfg"

static char g_dir[DIR_CAP];
static char g_override[DIR_CAP];
static bool g_has_override = false;

/* ---------- directory handling ---------- */

static void strip_trailing_seps(char *s)
{
    size_t n = strlen(s);
    while (n > 1 && IS_SEP(s[n - 1])) s[--n] = '\0';
}

static bool copy_dir(char *out, const char *src)
{
    if (strlen(src) >= DIR_CAP) return false;
    strcpy(out, src);
    strip_trailing_seps(out);
    return true;
}

static bool join_dir(char *out, const char *base, const char *tail)
{
    int n = snprintf(out, DIR_CAP, "%s%s", base, tail);
    return n > 0 && n < DIR_CAP;
}

static bool resolve_default(char *out)
{
    const char *env = getenv("CARO_DATA_DIR");
    if (env && *env) return copy_dir(out, env);
#ifdef _WIN32
    {
        const char *app = getenv("APPDATA");
        return app && *app && join_dir(out, app, "\\caro");
    }
#elif defined(__APPLE__)
    {
        const char *home = getenv("HOME");
        return home && *home && join_dir(out, home, "/Library/Application Support/caro");
    }
#else
    {
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *home;
        if (xdg && *xdg) return join_dir(out, xdg, "/caro");
        home = getenv("HOME");
        return home && *home && join_dir(out, home, "/.local/share/caro");
    }
#endif
}

const char *storage_dir(void)
{
    if (g_has_override) {
        strcpy(g_dir, g_override);
    } else if (!resolve_default(g_dir)) {
        strcpy(g_dir, "caro");
    }
    return g_dir;
}

void storage_set_dir(const char *dir)
{
    if (!dir || !*dir || strlen(dir) >= DIR_CAP) {
        g_has_override = false;
        return;
    }
    strcpy(g_override, dir);
    strip_trailing_seps(g_override);
    g_has_override = true;
}

/* Create the data directory and any missing parents. Failures surface later as fopen errors. */
static void ensure_dir(void)
{
    char buf[DIR_CAP];
    char *p;
    strcpy(buf, storage_dir());
    for (p = buf + 1; *p; p++) {
        if (IS_SEP(*p)) {
            char sep = *p;
            *p = '\0';
            CARO_MKDIR(buf);
            *p = sep;
        }
    }
    CARO_MKDIR(buf);
}

static bool build_path(char *out, const char *name)
{
    int n = snprintf(out, PATH_CAP, "%s/%s", storage_dir(), name);
    return n > 0 && n < PATH_CAP;
}

static bool slot_path(char *out, int slot)
{
    char name[32];
    if (slot < 0 || slot >= SAVE_SLOTS) return false;
    snprintf(name, sizeof name, "slot%02d.sav", slot + 1);
    return build_path(out, name);
}

static bool tmp_path_of(char *out, const char *path)
{
    int n = snprintf(out, PATH_CAP, "%s.tmp", path);
    return n > 0 && n < PATH_CAP;
}

/* Move a fully written .tmp file over the target. */
static bool commit_file(const char *tmp, const char *path)
{
#ifdef _WIN32
    remove(path); /* rename does not replace existing files on Windows */
#endif
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

/* ---------- text helpers ---------- */

/* Read one line without its line terminator. Returns 1 on success, 0 at EOF,
 * -1 if the line was too long (it is skipped entirely). */
static int read_line(FILE *f, char *buf, size_t cap)
{
    size_t n;
    if (!fgets(buf, (int)cap, f)) return 0;
    n = strlen(buf);
    if (n == cap - 1 && buf[n - 1] != '\n' && !feof(f)) {
        int ch;
        while ((ch = fgetc(f)) != EOF && ch != '\n') { }
        return -1;
    }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return 1;
}

/* Split "key=value" in place. False if there is no '='. */
static bool split_kv(char *line, char **key, char **val)
{
    char *eq = strchr(line, '=');
    if (!eq) return false;
    *eq = '\0';
    *key = line;
    *val = eq + 1;
    return true;
}

static bool parse_long(const char *v, long *out)
{
    char *end;
    long x;
    if (!*v) return false;
    errno = 0;
    x = strtol(v, &end, 10);
    if (errno != 0 || *end != '\0') return false;
    *out = x;
    return true;
}

static bool parse_i64(const char *v, int64_t *out)
{
    char *end;
    long long x;
    if (!*v) return false;
    errno = 0;
    x = strtoll(v, &end, 10);
    if (errno != 0 || *end != '\0') return false;
    *out = (int64_t)x;
    return true;
}

static bool parse_double(const char *v, double *out)
{
    char *end;
    double x;
    if (!*v) return false;
    x = strtod(v, &end);
    if (end == v || *end != '\0') return false;
    *out = x;
    return true;
}

static bool parse_flag(const char *v, bool *out)
{
    if (strcmp(v, "1") == 0) *out = true;
    else if (strcmp(v, "0") == 0) *out = false;
    else return false;
    return true;
}

/* Names may hold spaces and UTF-8. Bytes that would break the format ('%', '=', control
 * characters, DEL) are written as %XX. Everything else is written as-is. */
static void put_text(FILE *f, const char *key, const char *val, size_t max)
{
    size_t i;
    fprintf(f, "%s=", key);
    for (i = 0; i < max && val[i]; i++) {
        unsigned char c = (unsigned char)val[i];
        if (c == '%' || c == '=' || c < 0x20 || c == 0x7f) fprintf(f, "%%%02X", (unsigned)c);
        else fputc(c, f);
    }
    fputc('\n', f);
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Inverse of put_text. False on bad escapes or if the result does not fit in cap bytes
 * including the NUL. */
static bool text_decode(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    while (*in) {
        char c;
        if (*in == '%') {
            int hi = hex_value(in[1]);
            int lo = hi < 0 ? -1 : hex_value(in[2]);
            if (lo < 0) return false;
            c = (char)(hi * 16 + lo);
            in += 3;
        } else {
            c = *in++;
        }
        if (n + 1 >= cap) return false;
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

/* ---------- settings ---------- */

void settings_default(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->lang = LANG_EN;
    s->bgm_volume = 0.7f;
    s->sfx_volume = 0.8f;
    strcpy(s->profile[0].name, "Fireboy");
    s->profile[0].avatar = 0;
    strcpy(s->profile[1].name, "Watergirl");
    s->profile[1].avatar = 1;
    s->last_mode = MODE_PVE;
    s->last_board = 10;
    s->last_diff = DIFF_MEDIUM;
}

static float clamp_unit(double v, float fallback)
{
    if (v != v) return fallback; /* NaN */
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    return (float)v;
}

static void set_profile_name(char *dst, const char *val)
{
    char tmp[PLAYER_NAME_MAX];
    if (text_decode(val, tmp, sizeof tmp) && tmp[0] != '\0') strcpy(dst, tmp);
}

static void apply_setting(Settings *s, const char *key, const char *val)
{
    long lv;
    double dv;
    if (strcmp(key, "lang") == 0) {
        if (parse_long(val, &lv) && lv >= 0 && lv < LANG_COUNT) s->lang = (Lang)lv;
    } else if (strcmp(key, "bgm_volume") == 0) {
        if (parse_double(val, &dv)) s->bgm_volume = clamp_unit(dv, s->bgm_volume);
    } else if (strcmp(key, "sfx_volume") == 0) {
        if (parse_double(val, &dv)) s->sfx_volume = clamp_unit(dv, s->sfx_volume);
    } else if (strcmp(key, "bgm_muted") == 0) {
        parse_flag(val, &s->bgm_muted);
    } else if (strcmp(key, "sfx_muted") == 0) {
        parse_flag(val, &s->sfx_muted);
    } else if (strcmp(key, "reduced_motion") == 0) {
        parse_flag(val, &s->reduced_motion);
    } else if (strcmp(key, "fullscreen") == 0) {
        parse_flag(val, &s->fullscreen);
    } else if (strcmp(key, "p1_name") == 0) {
        set_profile_name(s->profile[0].name, val);
    } else if (strcmp(key, "p2_name") == 0) {
        set_profile_name(s->profile[1].name, val);
    } else if (strcmp(key, "p1_avatar") == 0 || strcmp(key, "p2_avatar") == 0) {
        int idx = key[1] == '1' ? 0 : 1;
        if (parse_long(val, &lv)) {
            if (lv < 0) lv = 0;
            if (lv > AVATAR_COUNT - 1) lv = AVATAR_COUNT - 1;
            s->profile[idx].avatar = (int)lv;
        }
    } else if (strcmp(key, "last_mode") == 0) {
        if (parse_long(val, &lv) && (lv == MODE_PVE || lv == MODE_PVP)) s->last_mode = (GameMode)lv;
    } else if (strcmp(key, "last_board") == 0) {
        if (parse_long(val, &lv) && (lv == 3 || lv == 5 || lv == 10)) s->last_board = (int)lv;
    } else if (strcmp(key, "last_diff") == 0) {
        if (parse_long(val, &lv) && lv >= 0 && lv < DIFF_COUNT) s->last_diff = (Difficulty)lv;
    } else if (strcmp(key, "block_rule") == 0) {
        parse_flag(val, &s->block_rule);
    }
}

bool settings_load(Settings *s)
{
    char path[PATH_CAP];
    char line[LINE_CAP];
    FILE *f;
    bool ok = false;
    int r;

    settings_default(s);
    if (!build_path(path, SETTINGS_FILE)) return false;
    f = fopen(path, "rb");
    if (!f) return false;
    r = read_line(f, line, sizeof line);
    if (r == 1 && strcmp(line, SETTINGS_MAGIC) == 0) {
        ok = true;
        while ((r = read_line(f, line, sizeof line)) != 0) {
            char *key, *val;
            if (r > 0 && split_kv(line, &key, &val)) apply_setting(s, key, val);
        }
    }
    fclose(f);
    return ok;
}

static void write_flag(FILE *f, const char *key, bool b)
{
    fprintf(f, "%s=%d\n", key, b ? 1 : 0);
}

bool settings_save(const Settings *s)
{
    char path[PATH_CAP], tmp[PATH_CAP];
    FILE *f;
    bool ok;

    if (!build_path(path, SETTINGS_FILE) || !tmp_path_of(tmp, path)) return false;
    ensure_dir();
    f = fopen(tmp, "wb");
    if (!f) return false;
    fprintf(f, "%s\n", SETTINGS_MAGIC);
    fprintf(f, "lang=%d\n", (int)s->lang);
    fprintf(f, "bgm_volume=%.9g\n", (double)s->bgm_volume);
    fprintf(f, "sfx_volume=%.9g\n", (double)s->sfx_volume);
    write_flag(f, "bgm_muted", s->bgm_muted);
    write_flag(f, "sfx_muted", s->sfx_muted);
    write_flag(f, "reduced_motion", s->reduced_motion);
    write_flag(f, "fullscreen", s->fullscreen);
    put_text(f, "p1_name", s->profile[0].name, PLAYER_NAME_MAX);
    fprintf(f, "p1_avatar=%d\n", s->profile[0].avatar);
    put_text(f, "p2_name", s->profile[1].name, PLAYER_NAME_MAX);
    fprintf(f, "p2_avatar=%d\n", s->profile[1].avatar);
    fprintf(f, "last_mode=%d\n", (int)s->last_mode);
    fprintf(f, "last_board=%d\n", s->last_board);
    fprintf(f, "last_diff=%d\n", (int)s->last_diff);
    write_flag(f, "block_rule", s->block_rule);
    ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
    return commit_file(tmp, path);
}

/* ---------- game validation ---------- */

static bool name_ok(const char *name)
{
    return memchr(name, '\0', PLAYER_NAME_MAX) != NULL;
}

/* Rebuild g->board by replaying the move list and check that the whole game is consistent:
 * legal moves, alternating players, no play after a win or a full board, and a status,
 * winner and turn that match the last move. On success g->board and g->win_line are set.
 * Used on load (fields come from the file) and on save (fields come from the caller). */
static bool game_check(Game *g)
{
    const int n = g->board.n;
    const int cnt = g->move_count;
    Board b;
    WinLine last_line;
    bool last_won = false;
    bool full;
    int i;

    if (g->mode != MODE_PVE && g->mode != MODE_PVP) return false;
    if (g->diff < 0 || g->diff >= DIFF_COUNT) return false;
    if (n < 3 || n > CARO_MAX_N) return false;
    if (g->ai_player != (g->mode == MODE_PVE ? 1 : -1)) return false;
    for (i = 0; i < 2; i++) {
        if (!name_ok(g->players[i].name)) return false;
        if (g->players[i].avatar < 0 || g->players[i].avatar >= AVATAR_COUNT) return false;
    }
    if (cnt < 0 || cnt > GAME_MAX_MOVES) return false;
    for (i = 0; i < 2; i++) {
        /* written as positive comparisons so NaN is rejected too */
        if (!(g->clock[i] >= -GAME_CLOCK_SECONDS && g->clock[i] <= GAME_CLOCK_SECONDS)) return false;
    }
    if (!(g->elapsed >= 0.0 && g->elapsed < 1e12)) return false;
    if (g->start_time < 0) return false;
    if (g->turn != 0 && g->turn != 1) return false;
    if (g->status != GS_PLAYING && g->status != GS_OVER) return false;

    board_init(&b, n, g->board.block_rule);
    memset(&last_line, 0, sizeof last_line);
    for (i = 0; i < cnt; i++) {
        Pos p = g->moves[i];
        Cell who = (i % 2 == 0) ? CELL_X : CELL_O;
        WinLine line;
        bool won;
        if (!board_place(&b, p.r, p.c, who)) return false;
        won = rules_check_win_at(&b, p.r, p.c, &line);
        if (i < cnt - 1) {
            /* the game would have ended here */
            if (won || board_full(&b)) return false;
        } else {
            last_won = won;
            if (won) last_line = line;
        }
    }
    full = board_full(&b);

    if (g->status == GS_PLAYING) {
        if (last_won || full) return false;
        if (g->turn != cnt % 2) return false;
        if (g->winner != -1 || g->end_reason != END_NONE) return false;
    } else {
        switch (g->end_reason) {
        case END_LINE:
            if (!last_won || g->winner != (cnt - 1) % 2) return false;
            break;
        case END_DRAW:
            if (last_won || !full || g->winner != -1) return false;
            break;
        case END_TIMEOUT:
        case END_SURRENDER:
            if (last_won || full || (g->winner != 0 && g->winner != 1)) return false;
            break;
        default:
            return false;
        }
    }

    g->board = b;
    memset(&g->win_line, 0, sizeof g->win_line);
    if (g->status == GS_OVER && g->end_reason == END_LINE) g->win_line = last_line;
    return true;
}

/* ---------- game save files ---------- */

/* Keys that must appear in a save file, as bits. */
enum {
    K_MODE = 1u << 0, K_DIFF = 1u << 1, K_BOARD = 1u << 2, K_BLOCK = 1u << 3,
    K_P1NAME = 1u << 4, K_P1AV = 1u << 5, K_P2NAME = 1u << 6, K_P2AV = 1u << 7,
    K_AI = 1u << 8, K_TURN = 1u << 9, K_CLOCK1 = 1u << 10, K_CLOCK2 = 1u << 11,
    K_START = 1u << 12, K_ELAPSED = 1u << 13, K_STATUS = 1u << 14, K_REASON = 1u << 15,
    K_WINNER = 1u << 16, K_SAVED = 1u << 17
};
#define K_ALL ((1u << 18) - 1u)
#define MOVE_COORD_LIMIT 1000

static bool load_game_file(const char *path, Game *out, int64_t *saved_at)
{
    FILE *f;
    char line[LINE_CAP];
    Game g;
    unsigned seen = 0;
    int board_n = 0;
    bool block = false;
    int64_t saved = 0;
    int nmoves = 0;
    bool ok = false;
    int r;

    f = fopen(path, "rb");
    if (!f) return false;
    memset(&g, 0, sizeof g);

    r = read_line(f, line, sizeof line);
    if (r != 1 || strcmp(line, SAVE_MAGIC) != 0) goto done;

    for (;;) {
        char *key, *val;
        long lv;
        double dv;
        int64_t iv;

        r = read_line(f, line, sizeof line);
        if (r == 0) break;
        if (r < 0) goto done; /* over-long line: not something we wrote */
        if (!split_kv(line, &key, &val)) continue;

        if (strcmp(key, "mode") == 0) {
            if (!parse_long(val, &lv) || lv < 0 || lv > 1) goto done;
            g.mode = (GameMode)lv;
            seen |= K_MODE;
        } else if (strcmp(key, "diff") == 0) {
            if (!parse_long(val, &lv) || lv < 0 || lv >= DIFF_COUNT) goto done;
            g.diff = (Difficulty)lv;
            seen |= K_DIFF;
        } else if (strcmp(key, "board") == 0) {
            if (!parse_long(val, &lv) || lv < 3 || lv > CARO_MAX_N) goto done;
            board_n = (int)lv;
            seen |= K_BOARD;
        } else if (strcmp(key, "block") == 0) {
            if (!parse_flag(val, &block)) goto done;
            seen |= K_BLOCK;
        } else if (strcmp(key, "p1name") == 0) {
            if (!text_decode(val, g.players[0].name, PLAYER_NAME_MAX)) goto done;
            seen |= K_P1NAME;
        } else if (strcmp(key, "p2name") == 0) {
            if (!text_decode(val, g.players[1].name, PLAYER_NAME_MAX)) goto done;
            seen |= K_P2NAME;
        } else if (strcmp(key, "p1avatar") == 0 || strcmp(key, "p2avatar") == 0) {
            int idx = key[1] == '1' ? 0 : 1;
            if (!parse_long(val, &lv) || lv < 0 || lv >= AVATAR_COUNT) goto done;
            g.players[idx].avatar = (int)lv;
            seen |= idx == 0 ? K_P1AV : K_P2AV;
        } else if (strcmp(key, "ai") == 0) {
            if (!parse_long(val, &lv) || lv < -1 || lv > 1) goto done;
            g.ai_player = (int)lv;
            seen |= K_AI;
        } else if (strcmp(key, "turn") == 0) {
            if (!parse_long(val, &lv) || lv < 0 || lv > 1) goto done;
            g.turn = (int)lv;
            seen |= K_TURN;
        } else if (strcmp(key, "clock1") == 0 || strcmp(key, "clock2") == 0) {
            int idx = key[5] == '1' ? 0 : 1;
            if (!parse_double(val, &dv)) goto done;
            g.clock[idx] = dv;
            seen |= idx == 0 ? K_CLOCK1 : K_CLOCK2;
        } else if (strcmp(key, "start") == 0) {
            if (!parse_i64(val, &iv)) goto done;
            g.start_time = iv;
            seen |= K_START;
        } else if (strcmp(key, "elapsed") == 0) {
            if (!parse_double(val, &dv)) goto done;
            g.elapsed = dv;
            seen |= K_ELAPSED;
        } else if (strcmp(key, "status") == 0) {
            if (!parse_long(val, &lv) || lv < GS_PLAYING || lv > GS_OVER) goto done;
            g.status = (GameStatus)lv;
            seen |= K_STATUS;
        } else if (strcmp(key, "reason") == 0) {
            if (!parse_long(val, &lv) || lv < END_NONE || lv > END_SURRENDER) goto done;
            g.end_reason = (EndReason)lv;
            seen |= K_REASON;
        } else if (strcmp(key, "winner") == 0) {
            if (!parse_long(val, &lv) || lv < -1 || lv > 1) goto done;
            g.winner = (int)lv;
            seen |= K_WINNER;
        } else if (strcmp(key, "saved") == 0) {
            if (!parse_i64(val, &iv) || iv < 0) goto done;
            saved = iv;
            seen |= K_SAVED;
        } else if (strcmp(key, "move") == 0) {
            char *comma = strchr(val, ',');
            long mr, mc;
            if (!comma || nmoves >= GAME_MAX_MOVES) goto done;
            *comma = '\0';
            if (!parse_long(val, &mr) || !parse_long(comma + 1, &mc)) goto done;
            if (mr < -MOVE_COORD_LIMIT || mr > MOVE_COORD_LIMIT) goto done;
            if (mc < -MOVE_COORD_LIMIT || mc > MOVE_COORD_LIMIT) goto done;
            g.moves[nmoves].r = (int)mr;
            g.moves[nmoves].c = (int)mc;
            nmoves++;
        }
        /* unknown keys are ignored */
    }
    if (seen != K_ALL) goto done;

    g.move_count = nmoves;
    g.undo_requester = -1;
    g.afk = AFK_NONE;
    g.idle = 0.0;
    g.afk_left = 0.0;
    board_init(&g.board, board_n, block);
    if (!game_check(&g)) goto done;

    *out = g;
    if (saved_at) *saved_at = saved;
    ok = true;

done:
    fclose(f);
    return ok;
}

static void write_game_file(FILE *f, const Game *g, int64_t saved_at)
{
    int i;
    fprintf(f, "%s\n", SAVE_MAGIC);
    fprintf(f, "mode=%d\n", (int)g->mode);
    fprintf(f, "diff=%d\n", (int)g->diff);
    fprintf(f, "board=%d\n", g->board.n);
    write_flag(f, "block", g->board.block_rule);
    put_text(f, "p1name", g->players[0].name, PLAYER_NAME_MAX);
    fprintf(f, "p1avatar=%d\n", g->players[0].avatar);
    put_text(f, "p2name", g->players[1].name, PLAYER_NAME_MAX);
    fprintf(f, "p2avatar=%d\n", g->players[1].avatar);
    fprintf(f, "ai=%d\n", g->ai_player);
    fprintf(f, "turn=%d\n", g->turn);
    fprintf(f, "clock1=%.17g\n", g->clock[0]);
    fprintf(f, "clock2=%.17g\n", g->clock[1]);
    fprintf(f, "start=%lld\n", (long long)g->start_time);
    fprintf(f, "elapsed=%.17g\n", g->elapsed);
    fprintf(f, "status=%d\n", (int)g->status);
    fprintf(f, "reason=%d\n", (int)g->end_reason);
    fprintf(f, "winner=%d\n", g->winner);
    fprintf(f, "saved=%lld\n", (long long)saved_at);
    for (i = 0; i < g->move_count; i++)
        fprintf(f, "move=%d,%d\n", g->moves[i].r, g->moves[i].c);
}

bool storage_save_game(int slot, const Game *g)
{
    char path[PATH_CAP], tmp[PATH_CAP];
    Game check;
    FILE *f;
    bool ok;

    if (!g) return false;
    check = *g;
    if (!game_check(&check)) return false;
    if (!slot_path(path, slot) || !tmp_path_of(tmp, path)) return false;

    ensure_dir();
    f = fopen(tmp, "wb");
    if (!f) return false;
    write_game_file(f, g, (int64_t)time(NULL));
    ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
    return commit_file(tmp, path);
}

bool storage_load_game(int slot, Game *g)
{
    char path[PATH_CAP];
    if (!g || !slot_path(path, slot)) return false;
    return load_game_file(path, g, NULL);
}

bool storage_delete_slot(int slot)
{
    char path[PATH_CAP];
    if (!slot_path(path, slot)) return false;
    if (remove(path) == 0) return true;
    return errno == ENOENT;
}

void storage_list_slots(SlotInfo out[SAVE_SLOTS])
{
    int i;
    for (i = 0; i < SAVE_SLOTS; i++) {
        char path[PATH_CAP];
        Game g;
        int64_t saved = 0;

        memset(&out[i], 0, sizeof out[i]);
        memset(&g, 0, sizeof g);
        if (!slot_path(path, i) || !load_game_file(path, &g, &saved)) continue;
        out[i].used = true;
        out[i].saved_at = saved;
        out[i].mode = g.mode;
        out[i].diff = g.diff;
        out[i].board_n = g.board.n;
        out[i].move_count = g.move_count;
        memcpy(out[i].p1, g.players[0].name, PLAYER_NAME_MAX);
        memcpy(out[i].p2, g.players[1].name, PLAYER_NAME_MAX);
    }
}

int storage_first_free_slot(void)
{
    SlotInfo info[SAVE_SLOTS];
    int i;
    storage_list_slots(info);
    for (i = 0; i < SAVE_SLOTS; i++)
        if (!info[i].used) return i;
    return -1;
}

int storage_autosave_slot(void)
{
    SlotInfo info[SAVE_SLOTS];
    int i, oldest = -1;
    int free_slot = storage_first_free_slot();
    if (free_slot >= 0) return free_slot;
    storage_list_slots(info);
    for (i = 0; i < SAVE_SLOTS; i++) {
        if (!info[i].used) continue;
        if (oldest < 0 || info[i].saved_at < info[oldest].saved_at) oldest = i;
    }
    return oldest;
}

int storage_latest_slot(void)
{
    SlotInfo info[SAVE_SLOTS];
    int i, latest = -1;
    storage_list_slots(info);
    for (i = 0; i < SAVE_SLOTS; i++) {
        if (!info[i].used) continue;
        if (latest < 0 || info[i].saved_at > info[latest].saved_at) latest = i;
    }
    return latest;
}
