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

static int encode_one(const wchar_t *src, const KtszTools *tools,
                      const wchar_t *work)
{
    wchar_t in_copy[KTSZ_MAX_PATH];
    wchar_t out_dir[KTSZ_MAX_PATH];
    wchar_t ogg_dir[KTSZ_MAX_PATH];
    wchar_t combined[KTSZ_MAX_PATH];
    wchar_t patch[KTSZ_MAX_PATH];
    wchar_t oggre[KTSZ_MAX_PATH];
    wchar_t verify_ogg[KTSZ_MAX_PATH];
    wchar_t verify_bin[KTSZ_MAX_PATH];
    wchar_t archive[KTSZ_MAX_PATH];
    wchar_t archive_tmp[KTSZ_MAX_PATH];
    wchar_t log[KTSZ_MAX_PATH];
    wchar_t args[8192];
    wchar_t tmpbuf[64];
    uint64_t orig_size = 0, copy_size = 0, ogg_size = 0, archive_size = 0;
    uint32_t orig_crc = 0, copy_crc = 0, ogg_crc = 0;
    WStrList kv_files, ogg_files;
    wchar_t *bincat_args = NULL;
    int used_bincat = 0;
    int result = -1;

    wsl_init(&kv_files);
    wsl_init(&ogg_files);
    archive_tmp[0] = L'\0';

    pjoin(in_copy, KTSZ_MAX_PATH, work, L"in.ktsl2stbin");
    pjoin(out_dir, KTSZ_MAX_PATH, work, L"out");
    pjoin(ogg_dir, KTSZ_MAX_PATH, work, L"ogg");
    pjoin(combined, KTSZ_MAX_PATH, work, L"combined.ogg");
    pjoin(patch, KTSZ_MAX_PATH, work, L"patch.hdiff");
    pjoin(oggre, KTSZ_MAX_PATH, work, L"combined.oggre");
    pjoin(verify_ogg, KTSZ_MAX_PATH, work, L"verify.ogg");
    pjoin(verify_bin, KTSZ_MAX_PATH, work, L"verify.bin");

    wprintf(L"  %-30ls ... ", L"Reading original");
    fflush(stdout);

    if (crc32_file(src, &orig_crc, &orig_size) != 0 || orig_size == 0) {
        wprintf(L"FAILED\n");
        return -1;
    }

    if (copy_file_overwrite(src, in_copy) != 0) {
        wprintf(L"FAILED (cannot copy)\n");
        return -1;
    }

    if (crc32_file(in_copy, &copy_crc, &copy_size) != 0 ||
        copy_size != orig_size || copy_crc != orig_crc) {
        wprintf(L"FAILED (copy verification failed)\n");
        return -1;
    }

    wprintf(L"ok (%ls bytes)\n", size_str(tmpbuf, 64, orig_size));

    if (run_step_ok(L"Extracting KVS files", tools->kvs_extract,
                    L"\"in.ktsl2stbin\"", work, L"1_extract.log") != 0)
        return -1;

    if (list_files(out_dir, L"*.kvs", &kv_files) != 0 || kv_files.count == 0) {
        wprintf(L"  error: no .kvs files were extracted\n");
        return -1;
    }

    wprintf(L"    %lu KVS file(s)\n", (unsigned long)kv_files.count);

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"out");
    wstr_append(args, 8192, L" -o ");
    wstr_append_quoted(args, 8192, L"ogg");
    wstr_append(args, 8192, L" -q");

    if (run_step_ok(L"Converting KVS to Ogg", tools->kv2ogg, args,
                    work, L"2_kv2ogg.log") != 0)
        return -1;

    if (list_files(ogg_dir, L"*.ogg", &ogg_files) != 0 || ogg_files.count == 0) {
        wprintf(L"  error: no .ogg files were created\n");
        return -1;
    }

    wsl_sort(&ogg_files, natural_path_cmp);
    wprintf(L"    %lu Ogg file(s)\n", (unsigned long)ogg_files.count);

    if (tools->bincat[0]) {
        size_t cap = 65536;

        bincat_args = (wchar_t *)malloc(cap * sizeof(wchar_t));
        if (bincat_args) {
            bincat_args[0] = L'\0';

            for (size_t i = 0; i < ogg_files.count; i++) {
                wchar_t rel[KTSZ_MAX_PATH];

                if (pjoin(rel, KTSZ_MAX_PATH, L"ogg",
                          path_basename(ogg_files.items[i])) != 0)
                    continue;

                if (i)
                    wstr_append(bincat_args, cap, L" ");
                wstr_append_quoted(bincat_args, cap, rel);
            }

            wstr_append(bincat_args, cap, L" ");
            wstr_append_quoted(bincat_args, cap, L"combined.ogg");

            if (wcslen(bincat_args) < 28000)
                used_bincat = 1;
        }
    }

    wprintf(L"  %-30ls ... ", used_bincat
            ? L"Concatenating Ogg files (bincat)"
            : L"Concatenating Ogg files");
    fflush(stdout);

    if (used_bincat) {
        int code = -1;

        if (pjoin(log, KTSZ_MAX_PATH, work, L"3_bincat.log") != 0 ||
            run_tool(tools->bincat, bincat_args, work, log, &code) != 0 ||
            code != 0) {
            wprintf(L"retrying internally\n");
            used_bincat = 0;
            DeleteFileW(combined);
        }
    }

    if (!used_bincat) {
        if (concat_files(&ogg_files, combined) != 0) {
            wprintf(L"FAILED\n");
            goto done;
        }
    }

    {
        uint64_t sum = 0, actual = 0;

        for (size_t i = 0; i < ogg_files.count; i++) {
            uint64_t one = 0;
            if (get_file_size(ogg_files.items[i], &one) == 0)
                sum += one;
        }

        if (get_file_size(combined, &actual) != 0 || actual == 0 || actual != sum) {
            wprintf(L"FAILED (size mismatch)\n");
            goto done;
        }

        wprintf(L"ok (%ls bytes)\n", size_str(tmpbuf, 64, actual));
    }

    free(bincat_args);
    bincat_args = NULL;

    args[0] = L'\0';
    wstr_append(args, 8192, L"-c-lzma2 ");
    wstr_append_quoted(args, 8192, L"combined.ogg");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"in.ktsl2stbin");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"patch.hdiff");

    if (run_step_ok(L"Creating delta patch", tools->hdiffz, args,
                    work, L"4_hdiffz.log") != 0)
        goto done;

    {
        uint64_t patch_size = 0;
        if (get_file_size(patch, &patch_size) != 0 || patch_size == 0) {
            wprintf(L"  error: empty patch\n");
            goto done;
        }
        wprintf(L"    patch: %ls bytes\n", size_str(tmpbuf, 64, patch_size));
    }

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"combined.ogg");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"combined.oggre");

    if (run_step_ok(L"Compressing Ogg (OGGRE)", tools->oggre_enc, args,
                    work, L"5_oggre_enc.log") != 0)
        goto done;

    wprintf(L"  %-30ls ... ", L"Verifying archive");
    fflush(stdout);

    if (crc32_file(combined, &ogg_crc, &ogg_size) != 0) {
        wprintf(L"FAILED (cannot read combined Ogg)\n");
        goto done;
    }

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"combined.oggre");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"verify.ogg");

    if (pjoin(log, KTSZ_MAX_PATH, work, L"6_verify_oggre.log") != 0)
        goto verify_failed;

    {
        int code = -1;
        if (run_tool(tools->oggre_dec, args, work, log, &code) != 0 || code != 0) {
            wprintf(L"FAILED (OGGRE decode)\n");
            print_last_log_lines(log, 12);
            goto done;
        }
    }

    if (!same_file_contents(combined, verify_ogg)) {
        wprintf(L"FAILED (Ogg mismatch)\n");
        goto done;
    }

    args[0] = L'\0';
    wstr_append_quoted(args, 8192, L"verify.ogg");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"patch.hdiff");
    wstr_append(args, 8192, L" ");
    wstr_append_quoted(args, 8192, L"verify.bin");

    if (pjoin(log, KTSZ_MAX_PATH, work, L"7_verify_patch.log") != 0)
        goto verify_failed;

    {
        int code = -1;
        if (run_tool(tools->hpatchz, args, work, log, &code) != 0 || code != 0) {
            wprintf(L"FAILED (patch apply)\n");
            print_last_log_lines(log, 12);
            goto done;
        }
    }

    if (!same_file_contents(in_copy, verify_bin)) {
        wprintf(L"FAILED (reconstructed file mismatch)\n");
        goto done;
    }

    wprintf(L"ok\n");

    wstr_copy(archive, KTSZ_MAX_PATH, src);
    if (has_suffix_ci(archive, L".ktsl2stbin"))
        archive[wcslen(archive) - 11] = L'\0';

    {
        size_t before = wcslen(archive);
        wstr_append(archive, KTSZ_MAX_PATH, KTSZ_ARCHIVE_EXT);
        if (wcslen(archive) != before + 5) {
            wprintf(L"  error: output path too long\n");
            goto done;
        }
    }

    if (append_ext(archive_tmp, KTSZ_MAX_PATH, archive, L".tmp") != 0) {
        wprintf(L"  error: output path too long\n");
        goto done;
    }

    wprintf(L"  %-30ls ... ", L"Writing archive");
    fflush(stdout);

    if (ktsz_write_container(archive_tmp, patch, oggre, orig_crc) != 0) {
        wprintf(L"FAILED\n");
        goto done;
    }

    {
        KtszInfo check;
        uint64_t ps = 0, os = 0;

        if (ktsz_read_container(archive_tmp, &check) != 0 ||
            check.layout != 2 ||
            check.original_crc32 != orig_crc ||
            get_file_size(patch, &ps) != 0 ||
            get_file_size(oggre, &os) != 0 ||
            check.patch_size != ps ||
            check.oggre_size != os) {
            wprintf(L"FAILED (container check)\n");
            goto done;
        }
    }

    if (move_file_replace(archive_tmp, archive) != 0) {
        wprintf(L"FAILED (cannot rename)\n");
        goto done;
    }

    archive_tmp[0] = L'\0';

    if (get_file_size(archive, &archive_size) != 0)
        archive_size = 0;

    wprintf(L"ok (%ls bytes, %.1f%% of original)\n",
            size_str(tmpbuf, 64, archive_size),
            orig_size ? (double)archive_size * 100.0 / (double)orig_size : 0.0);

    if (DeleteFileW(src))
        wprintf(L"    original removed\n");
    else
        wprintf(L"    warning: compressed OK, but could not delete original (error %lu)\n",
                (unsigned long)GetLastError());

    result = 0;
    goto done;

verify_failed:
    wprintf(L"FAILED (cannot create log)\n");

done:
    if (result != 0 && archive_tmp[0])
        DeleteFileW(archive_tmp);

    free(bincat_args);
    wsl_free(&kv_files);
    wsl_free(&ogg_files);
    return result;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t exe_dir[KTSZ_MAX_PATH];
    KtszTools tools;
    WStrList files;
    int converted = 0;
    int failed = 0;

    (void)argc;
    (void)argv;

    setlocale(LC_ALL, "");
    SetConsoleTitleW(L"KTSL2STBIN Encoder");

    wprintf(L"KTSL2STBIN Encoder 1.0\n");
    wprintf(L"======================\n\n");

    if (exe_directory(exe_dir, KTSZ_MAX_PATH) != 0) {
        wprintf(L"error: cannot determine the folder of this exe\n");
        pause_if_console();
        return 1;
    }

    wprintf(L"Folder: %ls\n", exe_dir);

    if (!tools_prepare(&tools, 1)) {
        pause_if_console();
        return 1;
    }

    wprintf(L"Tools : embedded (extracted to %ls)\n", tools.tools_dir);
    wprintf(L"\n");

    wsl_init(&files);
    if (list_files(exe_dir, L"*.ktsl2stbin", &files) != 0) {
        wprintf(L"error: cannot scan the folder\n");
        wsl_free(&files);
        delete_tree(tools.tools_dir);
        pause_if_console();
        return 1;
    }

    if (files.count == 0) {
        wprintf(L"No .ktsl2stbin files found in this folder.\n");
        wsl_free(&files);
        delete_tree(tools.tools_dir);
        pause_if_console();
        return 0;
    }

    wsl_sort(&files, ci_path_cmp);
    wprintf(L"Found %lu .ktsl2stbin file(s).\n", (unsigned long)files.count);

    for (size_t i = 0; i < files.count; i++) {
        wchar_t work[KTSZ_MAX_PATH];

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

        if (encode_one(files.items[i], &tools, work) == 0) {
            converted++;
            delete_tree(work);
        } else {
            failed++;
            wprintf(L"  FAILED - original file was kept.\n");
            wprintf(L"  Temp folder: %ls\n", work);
        }
    }

    wprintf(L"\nDone: %d compressed, %d failed.\n", converted, failed);

    delete_tree(tools.tools_dir);
    wsl_free(&files);
    pause_if_console();
    return failed ? 1 : 0;
}