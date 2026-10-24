/*
 * ktsz.h - shared definitions for the KTSL2STBIN encoder/decoder
 */

#ifndef KTSZ_H
#define KTSZ_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define KTSZ_MAGIC          "KTSZ"
#define KTSZ_VERSION        2u      /* current (12-byte footer) */
#define KTSZ_VERSION_LEGACY 1u      /* old 80-byte header, still readable */
#define KTSZ_FOOTER_SIZE    12u
#define KTSZ_HEADER_SIZE    0x50u   /* legacy 80-byte header */
#define KTSZ_ARCHIVE_EXT    L".ktsz"
#define KTSZ_MAX_PATH       32768

/*
 * Current container layout (all integers little-endian):
 *
 *   [ 0 .. P )        HDiffPatch delta
 *                     (old = combined Ogg, new = original .ktsl2stbin)
 *   [ P .. F - 12 )   OGGRE-compressed combined Ogg
 *   [ F - 12 ) u32    P (patch size)
 *   [ F - 8  ) u32    original CRC-32
 *   [ F - 4  ) char[4] "KTSZ"
 *
 * Total overhead: 12 bytes at the end of the file.
 *
 * Legacy layout (version 1, created by the first build) is still accepted
 * by the decoder:
 *
 *   0x00 "KTSZ", 0x04 version, 0x08 original size, 0x10 original CRC,
 *   0x18 patch offset, 0x20 patch size, 0x28 oggre offset, 0x30 oggre size,
 *   0x38 ogg size, 0x40 ogg CRC.  80-byte header.
 */
typedef struct {
    uint32_t layout;        /* 1 = legacy header, 2 = current footer */
    uint32_t version;
    uint64_t original_size; /* 0 if unknown (current format) */
    uint32_t original_crc32;
    uint32_t flags;
    uint64_t patch_offset;
    uint64_t patch_size;
    uint64_t oggre_offset;
    uint64_t oggre_size;
    uint64_t ogg_size;      /* 0 if unknown (current format) */
    uint32_t ogg_crc32;
    uint32_t reserved;
    uint64_t reserved2;
} KtszInfo;

typedef struct {
    wchar_t tools_dir[KTSZ_MAX_PATH];
    wchar_t kvs_extract[KTSZ_MAX_PATH];
    wchar_t kv2ogg[KTSZ_MAX_PATH];
    wchar_t bincat[KTSZ_MAX_PATH];
    wchar_t hdiffz[KTSZ_MAX_PATH];
    wchar_t hpatchz[KTSZ_MAX_PATH];
    wchar_t oggre_enc[KTSZ_MAX_PATH];
    wchar_t oggre_dec[KTSZ_MAX_PATH];
} KtszTools;

/* Growable list of wide strings. */
typedef struct {
    wchar_t **items;
    size_t    count;
    size_t    capacity;
} WStrList;

void wsl_init(WStrList *l);
void wsl_add_copy(WStrList *l, const wchar_t *s);
void wsl_free(WStrList *l);
void wsl_sort(WStrList *l, int (*cmp)(const void *, const void *));

int ci_path_cmp(const void *a, const void *b);
int natural_path_cmp(const void *a, const void *b);

/* Wide-string helpers. */
void wstr_copy(wchar_t *dst, size_t cap, const wchar_t *src);
void wstr_append(wchar_t *dst, size_t cap, const wchar_t *src);
void wstr_append_u64(wchar_t *dst, size_t cap, uint64_t value);
void wstr_append_quoted(wchar_t *dst, size_t cap, const wchar_t *arg);

const wchar_t *path_basename(const wchar_t *path);
int has_suffix_ci(const wchar_t *s, const wchar_t *suffix);
int pjoin(wchar_t *dst, size_t cap, const wchar_t *dir, const wchar_t *name);
int append_ext(wchar_t *dst, size_t cap, const wchar_t *path, const wchar_t *ext);

/* Files and directories. */
int file_exists(const wchar_t *path);
int dir_exists(const wchar_t *path);
int get_file_size(const wchar_t *path, uint64_t *size_out);
int copy_file_overwrite(const wchar_t *src, const wchar_t *dst);
int move_file_replace(const wchar_t *src, const wchar_t *dst);
int concat_files(const WStrList *files, const wchar_t *dst);
int ensure_directory(const wchar_t *path);
int delete_tree(const wchar_t *path);
int make_temp_dir(wchar_t *out, size_t cap);
int exe_directory(wchar_t *out, size_t cap);
int list_files(const wchar_t *dir, const wchar_t *pattern, WStrList *out);

/* CRC-32 (standard, reflected, poly 0xEDB88320). */
void crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);
int crc32_file(const wchar_t *path, uint32_t *crc_out, uint64_t *size_out);
int same_file_contents(const wchar_t *a, const wchar_t *b);

/* Embedded tools. */
int tools_prepare(KtszTools *tools, int for_encoder);
int run_tool(const wchar_t *exe, const wchar_t *args, const wchar_t *workdir,
             const wchar_t *log_path, int *exit_code);
void print_last_log_lines(const wchar_t *log_path, int max_lines);

/* Archive container. */
int ktsz_write_container(const wchar_t *out_path,
                         const wchar_t *patch_path,
                         const wchar_t *oggre_path,
                         uint32_t original_crc32);
int ktsz_read_container(const wchar_t *path, KtszInfo *info);
int ktsz_extract_part(const wchar_t *archive, uint64_t offset, uint64_t size,
                      const wchar_t *out_path);

#endif /* KTSZ_H */
