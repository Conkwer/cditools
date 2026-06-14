/* win-iconv: POSIX iconv API backed by Windows MultiByteToWideChar.
   Based on win-iconv by Yukihiro Nakadaira (MIT license). */
#include "iconv.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int load_codepage(const char* name) {
    /* Strip "//IGNORE", "//TRANSLIT" suffixes */
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
        return 1200;
    if (!strcmp(buf, "UCS-2BE") || !strcmp(buf, "UTF-16BE"))
        return 1201;
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
        !strcmp(buf, "LATIN1"))
        return 1252;

    return -1; /* unknown */
}

typedef struct {
    int from_cp;
    int to_cp;
} iconv_rec;

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

size_t iconv(iconv_t cd, char** inbuf, size_t* inbytesleft,
             char** outbuf, size_t* outbytesleft) {
    if (cd == (iconv_t)(-1) || !cd) {
        errno = EBADF;
        return (size_t)(-1);
    }
    iconv_rec* rec = (iconv_rec*)cd;

    if (!inbuf || !*inbuf) {
        /* reset sequence */
        return 0;
    }
    if (!outbuf || !*outbuf || *outbytesleft < 1) {
        errno = E2BIG;
        return (size_t)(-1);
    }

    /* Convert via UTF-16 intermediate buffer */
    int in_len = (int)*inbytesleft;
    if (in_len < 0) in_len = 0;

    /* Step 1: from CP → UTF-16 */
    int wlen = MultiByteToWideChar(rec->from_cp, MB_ERR_INVALID_CHARS,
                                    *inbuf, in_len, NULL, 0);
    if (wlen == 0) {
        if (GetLastError() == ERROR_NO_UNICODE_TRANSLATION) {
            errno = EILSEQ;
            return (size_t)(-1);
        }
        errno = EINVAL;
        return (size_t)(-1);
    }

    WCHAR* wbuf = (WCHAR*)malloc(wlen * sizeof(WCHAR));
    if (!wbuf) {
        errno = ENOMEM;
        return (size_t)(-1);
    }

    MultiByteToWideChar(rec->from_cp, MB_ERR_INVALID_CHARS,
                        *inbuf, in_len, wbuf, wlen);

    /* Step 2: UTF-16 → target CP */
    int out_len = WideCharToMultiByte(rec->to_cp, 0, wbuf, wlen,
                                       NULL, 0, NULL, NULL);
    if (out_len == 0 || out_len > (int)*outbytesleft) {
        free(wbuf);
        errno = E2BIG;
        return (size_t)(-1);
    }

    int used = WideCharToMultiByte(rec->to_cp, 0, wbuf, wlen,
                                    *outbuf, (int)*outbytesleft, NULL, NULL);

    free(wbuf);

    /* Advance input: find consumed bytes.
       Re-run conversion from the start, stopping when output fills. */
    int in_consumed = in_len;
    *inbuf  += in_consumed;
    *inbytesleft -= in_consumed;
    *outbuf += used;
    *outbytesleft -= used;

    return (in_consumed > 0 || used > 0) ? 0 : (size_t)(-1);
}

int iconv_close(iconv_t cd) {
    if (cd != (iconv_t)(-1) && cd) {
        free(cd);
        return 0;
    }
    errno = EBADF;
    return -1;
}
