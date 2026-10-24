#include "diagnostics.h"
#include "launcher.h"
#include "launcher_i18n.h"
#include "launcher_ui.h"
#include "ftp_server.h"
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <minizip/zip.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#define REPORT_FILES 64
#define FILE_LIMIT (16u * 1024 * 1024)
#define REPORT_LIMIT (64u * 1024 * 1024)
_Static_assert( REPORT_LIMIT >= FILE_LIMIT, "A report must hold one log" );

struct report_file { char name[256]; size_t size; time_t modified; };

static int random_bytes( unsigned char *bytes, size_t count )
{
#ifdef __SWITCH__
    randomGet( bytes, count );
    return 1;
#else
    FILE *file = fopen( "/dev/urandom", "rb" );
    int ok = file && fread( bytes, 1, count, file ) == count;
    if (file) fclose( file );
    return ok;
#endif
}

static int newest_first( const void *a, const void *b )
{
    const struct report_file *left = a, *right = b;
    return left->modified > right->modified ? -1 : left->modified < right->modified;
}

static int zip_text( zipFile zip, const char *name, const void *data, size_t size )
{
    int ok;
    if (zipOpenNewFileInZip64( zip, name, NULL, NULL, 0, NULL, 0, NULL,
                              Z_DEFLATED, Z_BEST_SPEED, 0 ) != ZIP_OK) return 0;
    ok = zipWriteInFileInZip( zip, data, (unsigned int)size ) == ZIP_OK;
    return zipCloseFileInZip( zip ) == ZIP_OK && ok;
}

int diagnostics_report( const struct wine_nx_launcher_options *options, char *path, size_t size )
{
    const char *runtime = options->runtime_dir;
    struct report_file files[REPORT_FILES];
    char folder[512], source[768], temporary[768], report[24576], stamp[32];
    unsigned char suffix[8], buffer[16384];
    struct dirent *entry;
    struct stat st;
    time_t now = time( NULL );
    struct tm calendar;
    DIR *dir;
    zipFile zip;
    size_t count = 0, used = 0, total = 0, i;
    int ok = 1, skipped = 0;

    if (snprintf( folder, sizeof(folder), "%s/reports", runtime ) >= (int)sizeof(folder) ||
        (mkdir( folder, 0777 ) && errno != EEXIST) || lstat( folder, &st ) || !S_ISDIR(st.st_mode)) return 0;
    if (!localtime_r( &now, &calendar ) || !random_bytes( suffix, sizeof(suffix) )) return 0;
    strftime( stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &calendar );
    if (snprintf( path, size, "%s/report-%s-%02x%02x%02x%02x.zip", folder, stamp,
                  suffix[0], suffix[1], suffix[2], suffix[3] ) >= (int)size) return 0;
    if (!lstat( path, &st ) || errno != ENOENT) return 0;
    if (snprintf( temporary, sizeof(temporary), "%s.partial", path ) >= (int)sizeof(temporary)) return 0;
    if (!lstat( temporary, &st ) || errno != ENOENT) return 0;
    if (snprintf( folder, sizeof(folder), "%s/logs", runtime ) >= (int)sizeof(folder)) return 0;
    if (!lstat( folder, &st ) && !S_ISDIR(st.st_mode)) return 0;
    if ((dir = opendir( folder )))
    {
        while ((entry = readdir( dir )))
        {
            size_t len = strlen( entry->d_name );
            int log = len > 4 && !strcmp( entry->d_name + len - 4, ".log" );
            if (!log && strcmp( entry->d_name, "stdout.txt" ) && strcmp( entry->d_name, "stderr.txt" )) continue;
            if (strpbrk( entry->d_name, "/\\\r\n" ) || len >= sizeof(files[0].name)) continue;
            if (snprintf( source, sizeof(source), "%s/%s", folder, entry->d_name ) >= (int)sizeof(source) ||
                lstat( source, &st ) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
                (uintmax_t)st.st_size > SIZE_MAX) continue;
            size_t slot = count;
            if (count == REPORT_FILES)
            {
                slot = 0;
                for (i = 1; i < count; i++)
                    if (files[i].modified < files[slot].modified) slot = i;
                skipped++;
                if (st.st_mtime <= files[slot].modified) continue;
            }
            else count++;
            strcpy( files[slot].name, entry->d_name );
            files[slot].size = st.st_size;
            files[slot].modified = st.st_mtime;
        }
        closedir( dir );
    }
    qsort( files, count, sizeof(files[0]), newest_first );
    if (!(zip = zipOpen64( temporary, APPEND_STATUS_CREATE ))) return 0;
    used = snprintf( report, sizeof(report),
                     "Autorun RU Diagnostics\nBuild: %s\nCreated: %s\n"
                     "UI language: %s\nGlobal verbose: %d\nGlobal profiler: %d\nAddress space: %d bits\nEmuMMC: %d\n"
                     "Limits: 16 MiB per log; 64 MiB per report; 64 logs.\n"
                     "Oversized logs contain their tail. stdin, raw settings, saves and game files are excluded.\n"
                     "Logs are not redacted: they may contain paths, arguments and application diagnostic output.\n\n",
                     options->build ? options->build : "unknown", stamp, launcher_language_code(),
                     options->verbose, options->profile, options->address_space_bits, options->emummc );
    for (i = 0; i < count && ok; i++)
    {
        FILE *file;
        size_t bytes = files[i].size < FILE_LIMIT ? files[i].size : FILE_LIMIT;
        size_t left = bytes;
        if (bytes > REPORT_LIMIT - total) { skipped++; continue; }
        if (snprintf( source, sizeof(source), "%s/%s", folder, files[i].name ) >= (int)sizeof(source)) { ok = 0; break; }
        if (!(file = fopen( source, "rb" ))) { skipped++; continue; }
        if (files[i].size > bytes && fseeko( file, files[i].size - bytes, SEEK_SET ))
        { fclose( file ); skipped++; continue; }
        if (zipOpenNewFileInZip64( zip, files[i].name, NULL, NULL, 0, NULL, 0, NULL,
                                  Z_DEFLATED, Z_BEST_SPEED, 0 ) != ZIP_OK)
        { fclose( file ); ok = 0; break; }
        while (left)
        {
            size_t take = left < sizeof(buffer) ? left : sizeof(buffer);
            size_t got = fread( buffer, 1, take, file );
            if (!got || zipWriteInFileInZip( zip, buffer, got ) != ZIP_OK) { ok = 0; break; }
            left -= got;
        }
        fclose( file );
        if (zipCloseFileInZip( zip ) != ZIP_OK) ok = 0;
        total += bytes;
        if (used < sizeof(report))
            used += snprintf( report + used, sizeof(report) - used, "%s: %zu bytes%s\n",
                              files[i].name, bytes, files[i].size > bytes ? " (tail, truncated)" : "" );
    }
    if (used < sizeof(report))
        used += snprintf( report + used, sizeof(report) - used, "\nSkipped logs: %d\n", skipped );
    if (used >= sizeof(report)) used = sizeof(report) - 1;
    if (!zip_text( zip, "report.txt", report, used )) ok = 0;
    if (zipClose( zip, NULL ) != ZIP_OK) ok = 0;
    return ok && !rename( temporary, path );
}

static int share_report( struct ui *ui, struct wine_nx_launcher_options *options )
{
    char archive[512];
    int ready;

    ui_progress_begin( ui );
    ui_progress_update( ui, launcher_tr("Diagnostic report"), launcher_tr("Preparing report..."), 0, 0 );
    ready = diagnostics_report( options, archive, sizeof(archive) );
    ui_progress_end( ui );
    if (!ready) { ui_message( ui, "Diagnostics", "Report could not be created." ); return 0; }
    return ftp_server_screen( ui, options );
}

int diagnostics_screen( struct ui *ui, struct wine_nx_launcher_options *options )
{
    struct ui_list list = {0};
    struct ui_row rows[3];
    int served = 0;
    for (;;)
    {
        enum ui_action action;
        memset( rows, 0, sizeof(rows) );
        snprintf( rows[0].label, sizeof(rows[0].label), "%s", launcher_tr("Verbose traces") );
        rows[0].kind = UI_ROW_SWITCH; rows[0].on = !!options->verbose; rows[0].adjustable = 1;
        rows[0].help = "Wine's traces go to autorun_runtime.log for every program without its own setting.";
        snprintf( rows[1].label, sizeof(rows[1].label), "%s", launcher_tr("Profiler") );
        rows[1].kind = UI_ROW_SWITCH; rows[1].on = !!options->profile; rows[1].adjustable = 1;
        rows[1].help = "[PROF] lines with where each thread spends its time.";
        snprintf( rows[2].label, sizeof(rows[2].label), "%s", launcher_tr("Create report and open FTP") );
        rows[2].kind = UI_ROW_ACTION;
        rows[2].help = "Create a ZIP with logs and diagnostic settings. No game files, saves or raw settings are included.";
        action = ui_list_run( ui, &list, "Diagnostics", NULL, rows, 3, 0 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return served;
        if (list.selection == 0) options->verbose = !options->verbose;
        else if (list.selection == 1) options->profile = !options->profile;
        else if (action == UI_ACTION_CHOOSE) { served |= share_report( ui, options ); ui_start_screen( ui ); }
    }
}
