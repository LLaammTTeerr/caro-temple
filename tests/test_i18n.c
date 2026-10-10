/* test_i18n.c - checks the English and Vietnamese UI string tables. */
#include <stdio.h>
#include <string.h>

#include "i18n.h"

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,         \
                    __LINE__, #cond);                                      \
            failures++;                                                    \
        }                                                                  \
    } while (0)

/* Strict UTF-8 validation: rejects overlong forms, surrogates, and code
 * points above U+10FFFF, and requires every multi-byte sequence to be
 * complete. */
static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned int cp;
        int extra;
        unsigned char c = *p;
        if (c < 0x80) {
            p++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            cp = c & 0x1F;
            extra = 1;
        } else if (c >= 0xE0 && c <= 0xEF) {
            cp = c & 0x0F;
            extra = 2;
        } else if (c >= 0xF0 && c <= 0xF4) {
            cp = c & 0x07;
            extra = 3;
        } else {
            return 0; /* stray continuation or invalid lead byte */
        }
        for (int i = 1; i <= extra; i++) {
            unsigned char cc = p[i];
            if ((cc & 0xC0) != 0x80) return 0; /* truncated sequence */
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (extra == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)))
            return 0;
        if (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) return 0;
        p += extra + 1;
    }
    return 1;
}

/* Collects the printf conversion characters (e.g. 'd', 's') in order,
 * skipping "%%". Returns the count, stores up to max entries in out. */
static int format_sequence(const char *s, char *out, int max)
{
    int n = 0;
    const char *p = s;
    while (*p) {
        if (*p != '%') {
            p++;
            continue;
        }
        p++;
        if (*p == '%') {
            p++;
            continue;
        }
        /* skip flags, width, precision, length modifiers */
        while (*p && strchr("-+ #0123456789.*lhzjt", *p)) p++;
        if (!*p) break;
        if (n < max) out[n] = *p;
        n++;
        p++;
    }
    return n;
}

static void test_all_keys_present(void)
{
    for (Lang l = LANG_EN; l < LANG_COUNT; l++) {
        i18n_set_lang(l);
        for (int i = 0; i < S_COUNT; i++) {
            const char *s = tr((StrId)i);
            CHECK(s != NULL);
            CHECK(s != NULL && s[0] != '\0');
        }
    }
    i18n_set_lang(LANG_EN);
}

static void test_vi_differs_from_en(void)
{
    int considered = 0, differs = 0;
    for (int i = 0; i < S_COUNT; i++) {
        if (i == S_GAME_TITLE || i == S_OK) continue;
        i18n_set_lang(LANG_EN);
        const char *en = tr((StrId)i);
        if (strchr(en, '%')) continue; /* format-only keys */
        i18n_set_lang(LANG_VI);
        const char *vi = tr((StrId)i);
        considered++;
        if (strcmp(en, vi) != 0) differs++;
    }
    i18n_set_lang(LANG_EN);
    CHECK(considered > 0);
    if (considered > 0) {
        printf("VI differs from EN for %d of %d keys\n", differs, considered);
        CHECK(differs * 10 >= considered * 9);
    }
}

static void test_format_specifiers_match(void)
{
    for (int i = 0; i < S_COUNT; i++) {
        char en_seq[16], vi_seq[16];
        i18n_set_lang(LANG_EN);
        const char *en = tr((StrId)i);
        int en_n = format_sequence(en, en_seq, 16);
        i18n_set_lang(LANG_VI);
        const char *vi = tr((StrId)i);
        int vi_n = format_sequence(vi, vi_seq, 16);
        if (en_n != vi_n) {
            fprintf(stderr, "key %d: specifier count differs (EN \"%s\", VI \"%s\")\n",
                    i, en, vi);
        }
        CHECK(en_n == vi_n);
        if (en_n == vi_n && en_n <= 16)
            CHECK(memcmp(en_seq, vi_seq, (size_t)en_n) == 0);
    }
    i18n_set_lang(LANG_EN);
}

static void test_vi_is_valid_utf8(void)
{
    i18n_set_lang(LANG_VI);
    for (int i = 0; i < S_COUNT; i++) {
        const char *s = tr((StrId)i);
        if (!utf8_valid(s))
            fprintf(stderr, "key %d: invalid UTF-8 in VI string \"%s\"\n", i, s);
        CHECK(utf8_valid(s));
    }
    i18n_set_lang(LANG_EN);
}

static void test_en_is_valid_utf8(void)
{
    i18n_set_lang(LANG_EN);
    for (int i = 0; i < S_COUNT; i++) CHECK(utf8_valid(tr((StrId)i)));
}

static void test_utf8_validator(void)
{
    CHECK(utf8_valid("Tiếng Việt"));
    CHECK(utf8_valid(""));
    CHECK(!utf8_valid("\xC3"));          /* truncated */
    CHECK(!utf8_valid("\x80"));          /* stray continuation */
    CHECK(!utf8_valid("\xC0\xAF"));      /* overlong */
    CHECK(!utf8_valid("\xED\xA0\x80"));  /* surrogate */
    CHECK(!utf8_valid("\xF4\x90\x80\x80")); /* > U+10FFFF */
}

static void test_tr_out_of_range(void)
{
    for (Lang l = LANG_EN; l < LANG_COUNT; l++) {
        i18n_set_lang(l);
        CHECK(strcmp(tr((StrId)-1), "") == 0);
        CHECK(strcmp(tr((StrId)S_COUNT), "") == 0);
        CHECK(strcmp(tr((StrId)(S_COUNT + 100)), "") == 0);
    }
    i18n_set_lang(LANG_EN);
}

static void test_set_lang_fallback(void)
{
    i18n_set_lang(LANG_VI);
    CHECK(i18n_lang() == LANG_VI);
    i18n_set_lang((Lang)99);
    CHECK(i18n_lang() == LANG_EN);
    CHECK(strcmp(tr(S_PLAY), "Play") == 0);

    i18n_set_lang(LANG_VI);
    i18n_set_lang((Lang)-1);
    CHECK(i18n_lang() == LANG_EN);
    i18n_set_lang(LANG_EN);
}

int main(void)
{
    test_utf8_validator();
    test_all_keys_present();
    test_vi_differs_from_en();
    test_format_specifiers_match();
    test_vi_is_valid_utf8();
    test_en_is_valid_utf8();
    test_tr_out_of_range();
    test_set_lang_fallback();

    if (failures) {
        fprintf(stderr, "test_i18n: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_i18n: all passed");
    return 0;
}
