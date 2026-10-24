#include "ktsz.h"

#include <io.h>
#include <locale.h>
#include <string.h>

static const wchar_t *size_str(wchar_t *buf, size_t cap, uint64_t v)
{
    buf[0] = L'\0';
    wstr_append_u64(buf, cap, v);
    return buf;
}

static void pause_if_console(void)
{
    if (_isatty(_fileno(stdin))) {
        wprintf(L"\nPress Enter to exit...");
        fflush(stdout);
        getwchar();
    }
}

static int run_step_ok(const wchar_t *label, const wchar_t *exe,
                       const wchar_t *args, const wchar_t *work,
                       const wchar_t *log_name)
{
    wchar_t log[KTSZ_MAX_PATH];
    int code = -1;

    if (pjoin(log, KTSZ_MAX_PATH, work, log_name) != 0)
        return -1;

    wprintf(L"  %-30ls ... ", label);
    fflush(stdout);

    if (run_tool(exe, args, work, log, &code) != 0) {
        wprintf(L"FAILED (cannot start %ls)\n", path_basename(exe));
        return -1;
    }

    if (code != 0) {
        wprintf(L"FAILED (exit code %d)\n", code);
        print_last_log_lines(log, 12);
        return -1;
    }

    wprintf(L"ok\n");
    return 0;
}

static int decode_one(const wchar_t *archive, const KtszTools *tools,
                      const wchar_t *work, int *skipped)
{
    wchar_t patch[KTSZ_MAX_PATH];
    wchar_t oggre[KTSZ_MAX_PATH];
    wchar_t combined[KTSZ_MAX_PATH];
    wchar_t restored[KTSZ_MAX_PATH];
    wchar_t out_path[KTSZ_MAX_PATH];
    wchar_t args[8192];
    wchar_t tmpbuf[64];
    KtszInfo info;
    uint64_t ogg_size = 0, restored_size = 0;
    uint32_t ogg_crc = 0, restored_crc = 0;
    int result = -1;

    *skipped = 0;

    pjoin(patch, KTSZ_MAX_PATH, work, L"patch.hdiff");
    pjoin(oggre, KTSZ_MAX_PATH, work, L"combined.oggre");
    pjoin(combined, KTSZ_MAX_PATH, work, L"combined.ogg");
    pjoin(restored, KTSZ_MAX_PATH, work, L"restored.ktsl2stbin");

    wprintf(L"  %-30ls ... ", L"Reading archive");
    fflush(stdout);

    if (ktsz_read_container(archive, &info) != 0) {
        wprintf(L"FAILED (not a valid KTSZ archive)\n");
        return -1;
    }

    wprintf(L"ok\n");

    if (info.layout == 2) {
        uint64_t archive_size = 0;
        get_file_size(archive, &archive_size);
        wprintf(L"    archive : %ls bytes\n", size_str(tmpbuf, 64, archive_size));
        wprintf(L"    patch   : %ls bytes\n", size_str(tmpbuf, 64, info.patch_size));
    } else {
        wprintf(L"    original: %ls bytes\n", size_str(tmpbuf, 64, info.original_size));
        wprintf(L"    ogg     : %ls bytes\n", size_str(tmpbuf, 64, info.ogg_size));
    }

    wprintf(L"  %-30ls ... ", L"Extracting archive data");
    fflush(stdout);

    if (ktsz_extract_part(archive, info.patch_offset, info.patch_size, patch) != 0 ||
        ktsz_extract_part(archive, info.oggre_offset, info.oggre_size, oggre) != 0) {
        wprintf(L"FAILED\n");
        goto done;
    }

    wprintf(L"ok\n");

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"combined.oggre");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"combined.ogg");

    if (run_step_ok(L"Decompressing Ogg (OGGRE)", tools->oggre_dec, args,
                    work, L"1_oggre_dec.log") != 0)
        goto done;

    if (info.ogg_size != 0) {
        wprintf(L"  %-30ls ... ", L"Verifying Ogg");
        fflush(stdout);

        if (crc32_file(combined, &ogg_crc, &ogg_size) != 0 ||
            ogg_size != info.ogg_size || ogg_crc != info.ogg_crc32) {
            wprintf(L"FAILED\n");
            goto done;
        }

        wprintf(L"ok\n");
    }

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"combined.ogg");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"patch.hdiff");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"restored.ktsl2stbin");

    if (run_step_ok(L"Applying delta patch", tools->hpatchz, args,
                    work, L"2_hpatchz.log") != 0)
        goto done;

    wprintf(L"  %-30ls ... ", L"Verifying original");
    fflush(stdout);

    if (crc32_file(restored, &restored_crc, &restored_size) != 0 ||
        (info.original_size != 0 && restored_size != info.original_size) ||
        restored_crc != info.original_crc32) {
        wprintf(L"FAILED\n");
        goto done;
    }

    wprintf(L"ok (%ls bytes)\n", size_str(tmpbuf, 64, restored_size));

    wstr_copy(out_path, KTSZ_MAX_PATH, archive);

    if (has_suffix_ci(out_path, KTSZ_ARCHIVE_EXT))
        out_path[wcslen(out_path) - wcslen(KTSZ_ARCHIVE_EXT)] = L'\0';

    if (!has_suffix_ci(out_path, L".ktsl2stbin"))
        wstr_append(out_path, KTSZ_MAX_PATH, L".ktsl2stbin");

    if (file_exists(out_path)) {
        if (same_file_contents(out_path, restored)) {
            wprintf(L"  Already restored: %ls\n", path_basename(out_path));
            *skipped = 1;
            result = 0;
            goto done;
        }
        wprintf(L"  Replacing existing %ls\n", path_basename(out_path));
    }

    if (move_file_replace(restored, out_path) != 0) {
        wprintf(L"  error: cannot write '%ls'\n", path_basename(out_path));
        goto done;
    }

    wprintf(L"  Restored: %ls\n", path_basename(out_path));
    result = 0;

done:
    if (result == 0) {
        if (!DeleteFileW(archive)) {
            SetFileAttributesW(archive, FILE_ATTRIBUTE_NORMAL);
            if (!DeleteFileW(archive)) {
                wprintf(L"  warning: could not delete archive (error %lu)\n",
                        (unsigned long)GetLastError());
                return result;
            }
        }
        wprintf(L"  Archive removed: %ls\n", path_basename(archive));
    }

    return result;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t exe_dir[KTSZ_MAX_PATH];
    KtszTools tools;
    WStrList files;
    int restored = 0;
    int skipped = 0;
    int failed = 0;

    (void)argc;
    (void)argv;

    setlocale(LC_ALL, "");
    SetConsoleTitleW(L"KTSL2STBIN Decoder");

    wprintf(L"KTSL2STBIN Decoder 1.0\n");
    wprintf(L"======================\n\n");

    if (exe_directory(exe_dir, KTSZ_MAX_PATH) != 0) {
        wprintf(L"error: cannot determine the folder of this exe\n");
        pause_if_console();
        return 1;
    }

    wprintf(L"Folder: %ls\n", exe_dir);

    if (!tools_find(&tools, exe_dir, 0)) {
        pause_if_console();
        return 1;
    }

    wprintf(L"Tools : %ls\n\n", tools.tools_dir);

    wsl_init(&files);
    if (list_files(exe_dir, L"*.ktsz", &files) != 0) {
        wprintf(L"error: cannot scan the folder\n");
        wsl_free(&files);
        pause_if_console();
        return 1;
    }

    if (files.count == 0) {
        wprintf(L"No .ktsz archives found in this folder.\n");
        wsl_free(&files);
        pause_if_console();
        return 0;
    }

    wsl_sort(&files, ci_path_cmp);
    wprintf(L"Found %lu archive(s).\n", (unsigned long)files.count);

    for (size_t i = 0; i < files.count; i++) {
        wchar_t work[KTSZ_MAX_PATH];
        int was_skipped = 0;
        int r;

        if (make_temp_dir(work, KTSZ_MAX_PATH) != 0) {
            wprintf(L"\n[%lu/%lu] %ls\n  error: cannot create a temp folder\n",
                    (unsigned long)(i + 1), (unsigned long)files.count,
                    path_basename(files.items[i]));
            failed++;
            continue;
        }

        wprintf(L"\n[%lu/%lu] %ls\n",
                (unsigned long)(i + 1), (unsigned long)files.count,
                path_basename(files.items[i]));

        r = decode_one(files.items[i], &tools, work, &was_skipped);

        if (r == 0) {
            if (was_skipped)
                skipped++;
            else
                restored++;
            delete_tree(work);
        } else {
            failed++;
            wprintf(L"  FAILED - archive was kept.\n");
            wprintf(L"  Temp folder: %ls\n", work);
        }
    }

    wprintf(L"\nDone: %d restored, %d already present, %d failed.\n",
            restored, skipped, failed);

    wsl_free(&files);
    pause_if_console();
    return failed ? 1 : 0;
}