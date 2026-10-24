/*
 * ktsz_common.c - shared code for the KTSL2STBIN encoder/decoder
 */

#include "ktsz.h"

#include <string.h>
#include <wctype.h>

/* ------------------------------------------------------------------ */
/* Wide strings and lists                                              */
/* ------------------------------------------------------------------ */

void wstr_copy(wchar_t *dst, size_t cap, const wchar_t *src)
{
    if (cap == 0)
        return;
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = L'\0';
}

void wstr_append(wchar_t *dst, size_t cap, const wchar_t *src)
{
    size_t len = wcslen(dst);
    if (len >= cap)
        return;
    size_t i = 0;
    while (src && src[i] && len + i + 1 < cap) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = L'\0';
}

void wstr_append_u64(wchar_t *dst, size_t cap, uint64_t value)
{
    wchar_t tmp[32];
    int n = 0;

    do {
        tmp[n++] = (wchar_t)(L'0' + (value % 10));
        value /= 10;
    } while (value && n < 31);

    size_t len = wcslen(dst);
    while (n > 0 && len + 1 < cap) {
        dst[len++] = tmp[--n];
        dst[len] = L'\0';
    }
}

/* Quote one argument following the standard CommandLineToArgvW rules. */
void wstr_append_quoted(wchar_t *dst, size_t cap, const wchar_t *arg)
{
    const wchar_t *p = arg;

    wstr_append(dst, cap, L"\"");

    while (*p) {
        size_t backslashes = 0;

        while (*p == L'\\') {
            backslashes++;
            p++;
        }

        if (*p == L'\0') {
            for (size_t i = 0; i < backslashes * 2; i++)
                wstr_append(dst, cap, L"\\");
            break;
        }

        if (*p == L'"') {
            for (size_t i = 0; i < backslashes * 2 + 1; i++)
                wstr_append(dst, cap, L"\\");
            wstr_append(dst, cap, L"\"");
            p++;
        } else {
            for (size_t i = 0; i < backslashes; i++)
                wstr_append(dst, cap, L"\\");
            wchar_t tmp[2] = { *p, L'\0' };
            wstr_append(dst, cap, tmp);
            p++;
        }
    }

    wstr_append(dst, cap, L"\"");
}

const wchar_t *path_basename(const wchar_t *path)
{
    const wchar_t *a = wcsrchr(path, L'\\');
    const wchar_t *b = wcsrchr(path, L'/');
    const wchar_t *s = a;

    if (!s || (b && b > s))
        s = b;

    return s ? s + 1 : path;
}

int has_suffix_ci(const wchar_t *s, const wchar_t *suffix)
{
    size_t n = wcslen(s);
    size_t m = wcslen(suffix);

    if (n < m)
        return 0;

    return _wcsicmp(s + n - m, suffix) == 0;
}

int pjoin(wchar_t *dst, size_t cap, const wchar_t *dir, const wchar_t *name)
{
    wchar_t tmp[KTSZ_MAX_PATH];

    wstr_copy(tmp, KTSZ_MAX_PATH, dir);

    size_t len = wcslen(tmp);
    if (len > 0 && tmp[len - 1] != L'\\' && tmp[len - 1] != L'/')
        wstr_append(tmp, KTSZ_MAX_PATH, L"\\");

    wstr_append(tmp, KTSZ_MAX_PATH, name);

    if (wcslen(tmp) + 1 > cap)
        return -1;

    wstr_copy(dst, cap, tmp);
    return 0;
}

int append_ext(wchar_t *dst, size_t cap, const wchar_t *path, const wchar_t *ext)
{
    wstr_copy(dst, cap, path);
    wstr_append(dst, cap, ext);

    return (wcslen(dst) == wcslen(path) + wcslen(ext)) ? 0 : -1;
}

void wsl_init(WStrList *l)
{
    l->items = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void wsl_add_owned(WStrList *l, wchar_t *s)
{
    if (l->count == l->capacity) {
        size_t cap = l->capacity ? l->capacity * 2 : 64;
        wchar_t **p = (wchar_t **)realloc(l->items, cap * sizeof(wchar_t *));

        if (!p) {
            wprintf(L"error: out of memory\n");
            exit(1);
        }

        l->items = p;
        l->capacity = cap;
    }

    l->items[l->count++] = s;
}

void wsl_add_copy(WStrList *l, const wchar_t *s)
{
    size_t n = wcslen(s) + 1;
    wchar_t *p = (wchar_t *)malloc(n * sizeof(wchar_t));

    if (!p) {
        wprintf(L"error: out of memory\n");
        exit(1);
    }

    memcpy(p, s, n * sizeof(wchar_t));
    wsl_add_owned(l, p);
}

void wsl_free(WStrList *l)
{
    for (size_t i = 0; i < l->count; i++)
        free(l->items[i]);

    free(l->items);
    wsl_init(l);
}

void wsl_sort(WStrList *l, int (*cmp)(const void *, const void *))
{
    if (l->count > 1)
        qsort(l->items, l->count, sizeof(wchar_t *), cmp);
}

int ci_path_cmp(const void *a, const void *b)
{
    const wchar_t *sa = *(const wchar_t *const *)a;
    const wchar_t *sb = *(const wchar_t *const *)b;
    return _wcsicmp(sa, sb);
}

static int is_digit_w(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

static int natural_cmp_string(const wchar_t *a, const wchar_t *b)
{
    const wchar_t *pa = a;
    const wchar_t *pb = b;

    while (*pa && *pb) {
        if (is_digit_w(*pa) && is_digit_w(*pb)) {
            const wchar_t *za = pa;
            const wchar_t *zb = pb;
            const wchar_t *ea;
            const wchar_t *eb;
            size_t la, lb;
            int c;

            while (*za == L'0')
                za++;
            while (*zb == L'0')
                zb++;

            ea = za;
            while (is_digit_w(*ea))
                ea++;
            eb = zb;
            while (is_digit_w(*eb))
                eb++;

            la = (size_t)(ea - za);
            lb = (size_t)(eb - zb);

            if (la != lb)
                return la < lb ? -1 : 1;

            c = wcsncmp(za, zb, la);
            if (c != 0)
                return c < 0 ? -1 : 1;

            pa = ea;
            pb = eb;
        } else {
            wchar_t ca = towlower(*pa);
            wchar_t cb = towlower(*pb);

            if (ca != cb)
                return ca < cb ? -1 : 1;

            pa++;
            pb++;
        }
    }

    if (*pa)
        return 1;
    if (*pb)
        return -1;
    return 0;
}

int natural_path_cmp(const void *a, const void *b)
{
    const wchar_t *sa = *(const wchar_t *const *)a;
    const wchar_t *sb = *(const wchar_t *const *)b;

    return natural_cmp_string(path_basename(sa), path_basename(sb));
}

/* ------------------------------------------------------------------ */
/* Files and directories                                               */
/* ------------------------------------------------------------------ */

int file_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

int dir_exists(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

int get_file_size(const wchar_t *path, uint64_t *size_out)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &fad))
        return -1;

    *size_out = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    return 0;
}

int copy_file_overwrite(const wchar_t *src, const wchar_t *dst)
{
    if (CopyFileW(src, dst, FALSE))
        return 0;

    if (GetLastError() == ERROR_ALREADY_EXISTS ||
        GetLastError() == ERROR_FILE_EXISTS) {
        SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(dst);
        if (CopyFileW(src, dst, FALSE))
            return 0;
    }

    return -1;
}

int move_file_replace(const wchar_t *src, const wchar_t *dst)
{
    if (file_exists(dst))
        SetFileAttributesW(dst, FILE_ATTRIBUTE_NORMAL);

    if (MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return 0;

    return -1;
}

int concat_files(const WStrList *files, const wchar_t *dst)
{
    FILE *out = _wfopen(dst, L"wb");
    unsigned char *buf;
    int result = -1;

    if (!out)
        return -1;

    buf = (unsigned char *)malloc(1 << 20);
    if (!buf) {
        fclose(out);
        return -1;
    }

    for (size_t i = 0; i < files->count; i++) {
        FILE *in = _wfopen(files->items[i], L"rb");
        size_t n;

        if (!in)
            goto done;

        while ((n = fread(buf, 1, 1 << 20, in)) > 0) {
            if (fwrite(buf, 1, n, out) != n) {
                fclose(in);
                goto done;
            }
        }

        if (ferror(in)) {
            fclose(in);
            goto done;
        }

        fclose(in);
    }

    if (fflush(out) != 0)
        goto done;

    result = 0;

done:
    free(buf);
    if (fclose(out) != 0)
        result = -1;
    return result;
}

int ensure_directory(const wchar_t *path)
{
    if (CreateDirectoryW(path, NULL))
        return 0;

    if (GetLastError() == ERROR_ALREADY_EXISTS && dir_exists(path))
        return 0;

    return -1;
}

int delete_tree(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    wchar_t pattern[KTSZ_MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    if (attr == INVALID_FILE_ATTRIBUTES)
        return 0;

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
        return DeleteFileW(path) ? 0 : -1;
    }

    if (pjoin(pattern, KTSZ_MAX_PATH, path, L"*") != 0)
        return -1;

    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child[KTSZ_MAX_PATH];

            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;

            if (pjoin(child, KTSZ_MAX_PATH, path, fd.cFileName) == 0)
                delete_tree(child);
        } while (FindNextFileW(h, &fd));

        FindClose(h);
    }

    return RemoveDirectoryW(path) ? 0 : -1;
}

int make_temp_dir(wchar_t *out, size_t cap)
{
    wchar_t base[KTSZ_MAX_PATH];

    if (GetTempPathW(KTSZ_MAX_PATH, base) == 0)
        return -1;

    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        wchar_t name[256];
        wchar_t full[KTSZ_MAX_PATH];

        name[0] = L'\0';
        wstr_append(name, 256, L"ktsz_");
        wstr_append_u64(name, 256, (uint64_t)GetCurrentProcessId());
        wstr_append(name, 256, L"_");
        wstr_append_u64(name, 256, (uint64_t)GetTickCount64());

        if (attempt > 0) {
            wstr_append(name, 256, L"_");
            wstr_append_u64(name, 256, attempt);
        }

        if (pjoin(full, KTSZ_MAX_PATH, base, name) != 0)
            return -1;

        if (CreateDirectoryW(full, NULL)) {
            if (wcslen(full) + 1 > cap)
                return -1;
            wstr_copy(out, cap, full);
            return 0;
        }

        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return -1;
    }

    return -1;
}

int exe_directory(wchar_t *out, size_t cap)
{
    wchar_t buf[KTSZ_MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, KTSZ_MAX_PATH);
    wchar_t *slash;

    if (n == 0 || n >= KTSZ_MAX_PATH)
        return -1;

    slash = wcsrchr(buf, L'\\');
    if (!slash)
        slash = wcsrchr(buf, L'/');

    if (!slash)
        return -1;

    if (slash == buf + 2 && buf[1] == L':')
        slash[1] = L'\0';       /* keep "C:\" */
    else
        *slash = L'\0';

    if (wcslen(buf) + 1 > cap)
        return -1;

    wstr_copy(out, cap, buf);
    return 0;
}

int list_files(const wchar_t *dir, const wchar_t *pattern, WStrList *out)
{
    wchar_t spec[KTSZ_MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;

    if (pjoin(spec, KTSZ_MAX_PATH, dir, pattern) != 0)
        return -1;

    h = FindFirstFileW(spec, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_NO_MORE_FILES)
            return 0;
        return -1;
    }

    do {
        wchar_t full[KTSZ_MAX_PATH];

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if (pjoin(full, KTSZ_MAX_PATH, dir, fd.cFileName) == 0)
            wsl_add_copy(out, full);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return 0;
}

/* ------------------------------------------------------------------ */
/* CRC-32                                                              */
/* ------------------------------------------------------------------ */

static uint32_t crc_table[256];
static int      crc_ready = 0;

void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t r = i;

        for (int b = 0; b < 8; b++)
            r = (r & 1u) ? (r >> 1) ^ 0xEDB88320u : (r >> 1);

        crc_table[i] = r;
    }

    crc_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;

    if (!crc_ready)
        crc32_init();

    while (len--)
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

    return crc;
}

int crc32_file(const wchar_t *path, uint32_t *crc_out, uint64_t *size_out)
{
    FILE *f;
    unsigned char *buf;
    uint32_t crc = 0xFFFFFFFF;
    uint64_t total = 0;
    size_t n;

    if (!crc_ready)
        crc32_init();

    f = _wfopen(path, L"rb");
    if (!f)
        return -1;

    buf = (unsigned char *)malloc(1 << 20);
    if (!buf) {
        fclose(f);
        return -1;
    }

    while ((n = fread(buf, 1, 1 << 20, f)) > 0) {
        crc = crc32_update(crc, buf, n);
        total += n;
    }

    if (ferror(f)) {
        free(buf);
        fclose(f);
        return -1;
    }

    free(buf);
    fclose(f);

    *crc_out = crc ^ 0xFFFFFFFFu;
    *size_out = total;
    return 0;
}

int same_file_contents(const wchar_t *a, const wchar_t *b)
{
    uint64_t sa, sb;
    FILE *fa, *fb;
    unsigned char *ba, *bb;
    int same = 0;

    if (get_file_size(a, &sa) != 0 || get_file_size(b, &sb) != 0)
        return 0;

    if (sa != sb)
        return 0;

    fa = _wfopen(a, L"rb");
    if (!fa)
        return 0;

    fb = _wfopen(b, L"rb");
    if (!fb) {
        fclose(fa);
        return 0;
    }

    ba = (unsigned char *)malloc(1 << 16);
    bb = (unsigned char *)malloc(1 << 16);
    if (!ba || !bb)
        goto done;

    while (sa > 0) {
        size_t want = sa > (1 << 16) ? (1 << 16) : (size_t)sa;
        size_t na = fread(ba, 1, want, fa);
        size_t nb = fread(bb, 1, want, fb);

        if (na != want || nb != want || memcmp(ba, bb, want) != 0)
            goto done;

        sa -= want;
    }

    same = (fgetc(fa) == EOF && fgetc(fb) == EOF);

done:
    free(ba);
    free(bb);
    fclose(fa);
    fclose(fb);
    return same;
}

/* ------------------------------------------------------------------ */
/* External tools                                                      */
/* ------------------------------------------------------------------ */

static int find_tool_in(wchar_t *out, size_t cap, const wchar_t *dir, const wchar_t *name)
{
    wchar_t path[KTSZ_MAX_PATH];

    if (pjoin(path, KTSZ_MAX_PATH, dir, name) != 0)
        return 0;

    if (!file_exists(path))
        return 0;

    if (wcslen(path) + 1 > cap)
        return 0;

    wstr_copy(out, cap, path);
    return 1;
}

int tools_find(KtszTools *tools, const wchar_t *exe_dir, int for_encoder)
{
    wchar_t tools_dir[KTSZ_MAX_PATH];
    wchar_t sub_dir[KTSZ_MAX_PATH];

    struct {
        wchar_t       *dst;
        const wchar_t *name;
        int            req_enc;
        int            req_dec;
    } entries[] = {
        { tools->kvs_extract, L"kvs_extract.exe", 1, 0 },
        { tools->kv2ogg,      L"kv2ogg.exe",      1, 0 },
        { tools->bincat,      L"bincat.exe",      0, 0 },
        { tools->hdiffz,      L"hdiffz.exe",      1, 0 },
        { tools->hpatchz,     L"hpatchz.exe",     1, 1 },
        { tools->oggre_enc,   L"OGGRE_enc.exe",   1, 0 },
        { tools->oggre_dec,   L"OGGRE_dec.exe",   1, 1 },
    };

    memset(tools, 0, sizeof(*tools));

    if (pjoin(tools_dir, KTSZ_MAX_PATH, exe_dir, L"Tools") != 0)
        return 0;

    if (!dir_exists(tools_dir))
        wstr_copy(tools_dir, KTSZ_MAX_PATH, exe_dir);

    wstr_copy(tools->tools_dir, KTSZ_MAX_PATH, tools_dir);

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        int required = for_encoder ? entries[i].req_enc : entries[i].req_dec;

        if (find_tool_in(entries[i].dst, KTSZ_MAX_PATH, tools_dir, entries[i].name))
            continue;

        if (find_tool_in(entries[i].dst, KTSZ_MAX_PATH, exe_dir, entries[i].name))
            continue;

        if (!wcscmp(entries[i].name, L"kv2ogg.exe")) {
            if (pjoin(sub_dir, KTSZ_MAX_PATH, exe_dir, L"kv2ogg") == 0 &&
                find_tool_in(entries[i].dst, KTSZ_MAX_PATH, sub_dir, entries[i].name))
                continue;
        }

        if (required) {
            wprintf(L"error: missing required tool: %ls\n", entries[i].name);
            wprintf(L"       looked in:\n");
            wprintf(L"         %ls\n", tools_dir);
            wprintf(L"         %ls\n", exe_dir);
            return 0;
        }
    }

    return 1;
}

int run_tool(const wchar_t *exe, const wchar_t *args, const wchar_t *workdir,
             const wchar_t *log_path, int *exit_code)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE hLog, hNul;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *cmd;
    size_t cap;
    BOOL ok;
    DWORD err;
    DWORD code = 1;

    if (!file_exists(exe))
        return -1;

    cap = wcslen(exe) + (args ? wcslen(args) : 0) + 64;
    cmd = (wchar_t *)malloc(cap * sizeof(wchar_t));
    if (!cmd)
        return -1;

    cmd[0] = L'\0';
    wstr_append_quoted(cmd, cap, exe);

    if (args && *args) {
        wstr_append(cmd, cap, L" ");
        wstr_append(cmd, cap, args);
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    hLog = CreateFileW(log_path, GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE) {
        free(cmd);
        return -1;
    }

    hNul = CreateFileW(L"NUL", GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                       OPEN_EXISTING, 0, NULL);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = (hNul != INVALID_HANDLE_VALUE) ? hNul : NULL;
    si.hStdOutput = hLog;
    si.hStdError = hLog;

    memset(&pi, 0, sizeof(pi));

    ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, workdir, &si, &pi);
    err = ok ? 0 : GetLastError();

    free(cmd);
    CloseHandle(hLog);
    if (hNul != INVALID_HANDLE_VALUE)
        CloseHandle(hNul);

    if (!ok) {
        SetLastError(err);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code)
        *exit_code = (int)code;

    return 0;
}

void print_last_log_lines(const wchar_t *log_path, int max_lines)
{
    FILE *f = _wfopen(log_path, L"rb");
    __int64 size, start;
    size_t n;
    char *buf = NULL;
    wchar_t *wbuf = NULL;
    int wlen;
    UINT cp = CP_UTF8;
    int lines = 0;
    wchar_t *p;

    if (!f)
        return;

    if (_fseeki64(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }

    size = _ftelli64(f);
    if (size <= 0) {
        fclose(f);
        return;
    }

    start = size > 8192 ? size - 8192 : 0;
    if (_fseeki64(f, start, SEEK_SET) != 0) {
        fclose(f);
        return;
    }

    n = (size_t)(size - start);
    buf = (char *)malloc(n + 1);
    if (!buf) {
        fclose(f);
        return;
    }

    n = fread(buf, 1, n, f);
    buf[n] = '\0';
    fclose(f);

    wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, buf, (int)n, NULL, 0);
    if (wlen <= 0) {
        cp = CP_ACP;
        wlen = MultiByteToWideChar(CP_ACP, 0, buf, (int)n, NULL, 0);
    }

    if (wlen <= 0) {
        free(buf);
        return;
    }

    wbuf = (wchar_t *)malloc(((size_t)wlen + 1) * sizeof(wchar_t));
    if (!wbuf) {
        free(buf);
        return;
    }

    MultiByteToWideChar(cp, 0, buf, (int)n, wbuf, wlen);
    wbuf[wlen] = L'\0';
    free(buf);

    for (wchar_t *q = wbuf; *q; q++)
        if (*q == L'\n')
            lines++;

    p = wbuf;
    if (lines > max_lines) {
        int skip = lines - max_lines;

        while (skip-- > 0) {
            wchar_t *nl = wcschr(p, L'\n');
            if (!nl)
                break;
            p = nl + 1;
        }
    }

    wprintf(L"  --- tool output ------------------------------------------\n");
    wprintf(L"%ls", p);
    if (wcslen(p) && p[wcslen(p) - 1] != L'\n')
        wprintf(L"\n");
    wprintf(L"  ----------------------------------------------------------\n");

    free(wbuf);
}

/* ------------------------------------------------------------------ */
/* Archive container                                                   */
/* ------------------------------------------------------------------ */

static void put_u32le(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static uint32_t get_u32le(const unsigned char *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64le(const unsigned char *p)
{
    return (uint64_t)get_u32le(p) | ((uint64_t)get_u32le(p + 4) << 32);
}

static int append_file_to(FILE *out, const wchar_t *path, uint64_t expected)
{
    FILE *in;
    unsigned char *buf;
    uint64_t left;
    int result = -1;

    if (get_file_size(path, &left) != 0 || left != expected)
        return -1;

    in = _wfopen(path, L"rb");
    if (!in)
        return -1;

    buf = (unsigned char *)malloc(1 << 20);
    if (!buf) {
        fclose(in);
        return -1;
    }

    while (left > 0) {
        size_t want = left > (1 << 20) ? (1 << 20) : (size_t)left;
        size_t got = fread(buf, 1, want, in);

        if (got != want || fwrite(buf, 1, got, out) != got)
            goto done;

        left -= got;
    }

    result = 0;

done:
    free(buf);
    fclose(in);
    return result;
}

int ktsz_write_container(const wchar_t *out_path,
                         const wchar_t *patch_path,
                         const wchar_t *oggre_path,
                         uint32_t original_crc32)
{
    unsigned char footer[KTSZ_FOOTER_SIZE];
    uint64_t patch_size, oggre_size;
    FILE *out;
    int result = -1;

    if (get_file_size(patch_path, &patch_size) != 0 ||
        get_file_size(oggre_path, &oggre_size) != 0)
        return -1;

    if (patch_size == 0 || oggre_size == 0 || patch_size > UINT32_MAX)
        return -1;

    out = _wfopen(out_path, L"wb");
    if (!out)
        return -1;

    if (append_file_to(out, patch_path, patch_size) != 0)
        goto done;

    if (append_file_to(out, oggre_path, oggre_size) != 0)
        goto done;

    put_u32le(footer + 0, (uint32_t)patch_size);
    put_u32le(footer + 4, original_crc32);
    memcpy(footer + 8, KTSZ_MAGIC, 4);

    if (fwrite(footer, 1, sizeof(footer), out) != sizeof(footer))
        goto done;

    if (fflush(out) != 0)
        goto done;

    result = 0;

done:
    if (fclose(out) != 0)
        result = -1;

    return result;
}

/*
 * Read the archive metadata.  The current format puts 12 bytes at the end:
 *     u32 patch_size, u32 original_crc32, "KTSZ"
 * Legacy archives (80-byte header at the start) are also accepted.
 */
int ktsz_read_container(const wchar_t *path, KtszInfo *info)
{
    unsigned char buf[KTSZ_HEADER_SIZE];
    FILE *f;
    uint64_t file_size;

    if (get_file_size(path, &file_size) != 0)
        return -1;

    f = _wfopen(path, L"rb");
    if (!f)
        return -1;

    /* ---- current format: 12-byte footer ---- */
    if (file_size >= KTSZ_FOOTER_SIZE) {
        unsigned char footer[KTSZ_FOOTER_SIZE];

        if (_fseeki64(f, (__int64)(file_size - KTSZ_FOOTER_SIZE), SEEK_SET) == 0 &&
            fread(footer, 1, sizeof(footer), f) == sizeof(footer) &&
            memcmp(footer + 8, KTSZ_MAGIC, 4) == 0) {

            uint32_t patch_size = get_u32le(footer + 0);
            uint64_t payload = file_size - KTSZ_FOOTER_SIZE;

            if (patch_size > 0 && (uint64_t)patch_size < payload) {
                memset(info, 0, sizeof(*info));
                info->layout        = 2;
                info->version       = KTSZ_VERSION;
                info->original_crc32= get_u32le(footer + 4);
                info->patch_offset  = 0;
                info->patch_size    = patch_size;
                info->oggre_offset  = patch_size;
                info->oggre_size    = payload - patch_size;

                fclose(f);
                return 0;
            }
        }
    }

    /* ---- legacy format: 80-byte header ---- */
    if (file_size < KTSZ_HEADER_SIZE) {
        fclose(f);
        return -1;
    }

    if (_fseeki64(f, 0, SEEK_SET) != 0 ||
        fread(buf, 1, KTSZ_HEADER_SIZE, f) != KTSZ_HEADER_SIZE) {
        fclose(f);
        return -1;
    }

    fclose(f);

    if (memcmp(buf, KTSZ_MAGIC, 4) != 0)
        return -1;

    memset(info, 0, sizeof(*info));
    info->layout        = 1;
    info->version       = get_u32le(buf + 0x04);
    info->original_size = get_u64le(buf + 0x08);
    info->original_crc32= get_u32le(buf + 0x10);
    info->flags         = get_u32le(buf + 0x14);
    info->patch_offset  = get_u64le(buf + 0x18);
    info->patch_size    = get_u64le(buf + 0x20);
    info->oggre_offset  = get_u64le(buf + 0x28);
    info->oggre_size    = get_u64le(buf + 0x30);
    info->ogg_size      = get_u64le(buf + 0x38);
    info->ogg_crc32     = get_u32le(buf + 0x40);
    info->reserved      = get_u32le(buf + 0x44);
    info->reserved2     = get_u64le(buf + 0x48);

    if (info->version != KTSZ_VERSION_LEGACY)
        return -1;
    if (info->original_size == 0 || info->ogg_size == 0)
        return -1;
    if (info->patch_offset < KTSZ_HEADER_SIZE || info->patch_size == 0)
        return -1;
    if (info->oggre_size == 0)
        return -1;
    if (info->patch_offset > UINT64_MAX - info->patch_size)
        return -1;
    if (info->oggre_offset < info->patch_offset + info->patch_size)
        return -1;
    if (info->oggre_offset > UINT64_MAX - info->oggre_size)
        return -1;

    if (info->patch_offset + info->patch_size > file_size)
        return -1;
    if (info->oggre_offset + info->oggre_size > file_size)
        return -1;

    return 0;
}

int ktsz_extract_part(const wchar_t *archive, uint64_t offset, uint64_t size,
                      const wchar_t *out_path)
{
    FILE *in, *out;
    unsigned char *buf;
    int result = -1;

    if (offset > (uint64_t)INT64_MAX)
        return -1;

    in = _wfopen(archive, L"rb");
    if (!in)
        return -1;

    if (_fseeki64(in, (__int64)offset, SEEK_SET) != 0) {
        fclose(in);
        return -1;
    }

    out = _wfopen(out_path, L"wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    buf = (unsigned char *)malloc(1 << 20);
    if (!buf)
        goto done;

    while (size > 0) {
        size_t want = size > (1 << 20) ? (1 << 20) : (size_t)size;
        size_t got = fread(buf, 1, want, in);

        if (got != want || fwrite(buf, 1, got, out) != got)
            goto done;

        size -= got;
    }

    if (fflush(out) != 0)
        goto done;

    result = 0;

done:
    free(buf);
    fclose(in);
    if (fclose(out) != 0)
        result = -1;

    return result;
}
