/* win-iconv: POSIX iconv API backed by Windows MultiByteToWideChar.
   Handles UTF-8, UCS-2BE, UCS-2LE, Shift-JIS, and all Win32 code pages.
   MIT-licensed — based on win-iconv by Yukihiro Nakadaira. */
#include "iconv.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Code page IDs for UTF-16 variants. Not valid for MultiByteToWideChar
   (which only accepts multi-byte CPs), so we handle them manually. */
#define CP_UTF16LE 1200
#define CP_UTF16BE 1201

static int is_utf16_cp(int cp) { return cp == CP_UTF16LE || cp == CP_UTF16BE; }

static int load_codepage(const char* name) {
    char buf[64];
    size_t n = strlen(name);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, name, n);
    buf[n] = 0;
    char* p = strstr(buf, "//");
    if (p) *p = 0;

    if (!strcmp(buf, "UTF-8") || !strcmp(buf, "UTF8"))
        return CP_UTF8;
    if (!strcmp(buf, "UCS-2LE") || !strcmp(buf, "UTF-16LE"))
        return CP_UTF16LE;
    if (!strcmp(buf, "UCS-2BE") || !strcmp(buf, "UTF-16BE"))
        return CP_UTF16BE;
    if (!strcmp(buf, "WCHAR_T"))
        return CP_UTF16LE; /* Windows wchar_t is UTF-16LE */
    if (!strcmp(buf, "ASCII"))
        return 20127;
    if (!strcmp(buf, "CP932") || !strcmp(buf, "SHIFT_JIS") ||
        !strcmp(buf, "SHIFT-JIS") || !strcmp(buf, "SJIS"))
        return 932;
    if (!strcmp(buf, "CP936") || !strcmp(buf, "GBK") || !strcmp(buf, "GB2312"))
        return 936;
    if (!strcmp(buf, "CP949") || !strcmp(buf, "EUC-KR"))
        return 949;
    if (!strcmp(buf, "CP950") || !strcmp(buf, "BIG5"))
        return 950;
    if (!strcmp(buf, "CP1252") || !strcmp(buf, "ISO-8859-1") ||
        !strcmp(buf, "LATIN1") || !strcmp(buf, "ISO_8859-1"))
        return 1252;
    if (buf[0] == 'C' && buf[1] == 'P')
        return atoi(buf + 2);
    return GetACP();
}

typedef struct { int from_cp; int to_cp; } iconv_rec;

iconv_t iconv_open(const char* tocode, const char* fromcode) {
    int from_cp = load_codepage(fromcode);
    int to_cp   = load_codepage(tocode);
    if (from_cp < 0 || to_cp < 0) {
        errno = EINVAL;
        return (iconv_t)(-1);
    }
    iconv_rec* rec = (iconv_rec*)malloc(sizeof(iconv_rec));
    if (!rec) return (iconv_t)(-1);
    rec->from_cp = from_cp;
    rec->to_cp   = to_cp;
    return (iconv_t)rec;
}

/* Convert a buffer from its source encoding into WCHAR (UTF-16LE).
   Returns number of WCHARs produced, or -1 on error. */
static int to_wide(int cp, const char* in, int in_len, WCHAR** out) {
    *out = NULL;
    int wlen;

    if (is_utf16_cp(cp)) {
        /* Fixed-width: 2 bytes per WCHAR. Must be even length. */
        if (in_len & 1) { errno = EINVAL; return -1; }
        wlen = in_len / 2;
        WCHAR* wbuf = (WCHAR*)malloc(wlen * sizeof(WCHAR));
        if (!wbuf) { errno = ENOMEM; return -1; }
        const unsigned char* src = (const unsigned char*)in;
        if (cp == CP_UTF16BE) {
            for (int i = 0; i < wlen; i++)
                wbuf[i] = (src[2*i] << 8) | src[2*i + 1];
        } else {
            for (int i = 0; i < wlen; i++)
                wbuf[i] = (src[2*i + 1] << 8) | src[2*i];
        }
        *out = wbuf;
        return wlen;
    }

    /* Multi-byte: use Win32 API */
    wlen = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, in, in_len, NULL, 0);
    if (wlen == 0) {
        if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION) { errno = EILSEQ; return -1; }
        errno = EINVAL;
        return -1;
    }
    WCHAR* wbuf = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    if (!wbuf) { errno = ENOMEM; return -1; }
    MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, in, in_len, wbuf, wlen);
    *out = wbuf;
    return wlen;
}

/* Convert WCHAR buffer to target encoding. Returns bytes written, or -1. */
static int from_wide(int cp, const WCHAR* wbuf, int wlen, char* out, int out_max) {
    if (is_utf16_cp(cp)) {
        int need = wlen * 2;
        if (need > out_max) { errno = E2BIG; return -1; }
        unsigned char* dst = (unsigned char*)out;
        if (cp == CP_UTF16BE) {
            for (int i = 0; i < wlen; i++) {
                unsigned short ch = (unsigned short)wbuf[i];
                dst[2*i]     = (unsigned char)(ch >> 8);
                dst[2*i + 1] = (unsigned char)(ch & 0xFF);
            }
        } else {
            for (int i = 0; i < wlen; i++) {
                unsigned short ch = (unsigned short)wbuf[i];
                dst[2*i + 1] = (unsigned char)(ch >> 8);
                dst[2*i]     = (unsigned char)(ch & 0xFF);
            }
        }
        return need;
    }
    int n = WideCharToMultiByte(cp, 0, wbuf, wlen, out, out_max, NULL, NULL);
    if (n == 0) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) { errno = E2BIG; return -1; }
        errno = EILSEQ;
        return -1;
    }
    return n;
}

size_t iconv(iconv_t cd, char** inbuf, size_t* inbytesleft,
             char** outbuf, size_t* outbytesleft) {
    if (cd == (iconv_t)(-1) || !cd) { errno = EBADF; return (size_t)(-1); }
    iconv_rec* rec = (iconv_rec*)cd;

    /* Reset sequence */
    if (!inbuf || !*inbuf) return 0;
    if (!outbuf || !*outbuf || *outbytesleft < 1) { errno = E2BIG; return (size_t)(-1); }

    int in_len = (int)*inbytesleft;
    if (in_len < 0) in_len = 0;
    if (in_len == 0) return 0;

    /* Step 1: source → WCHAR (UTF-16LE intermediate) */
    WCHAR* wbuf = NULL;
    int wlen = to_wide(rec->from_cp, *inbuf, in_len, &wbuf);
    if (wlen < 0) { free(wbuf); return (size_t)(-1); }

    /* Step 2: WCHAR → target */
    int out_max = (int)*outbytesleft;
    int used = from_wide(rec->to_cp, wbuf, wlen, *outbuf, out_max);
    if (used < 0) { free(wbuf); return (size_t)(-1); }

    free(wbuf);
    *inbuf  += in_len;
    *inbytesleft -= in_len;
    *outbuf += used;
    *outbytesleft -= used;
    return 0;
}

int iconv_close(iconv_t cd) {
    if (cd != (iconv_t)(-1) && cd) { free(cd); return 0; }
    errno = EBADF;
    return -1;
}
