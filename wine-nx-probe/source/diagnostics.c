#include "diagnostics.h"
#include "launcher.h"
#include "launcher_i18n.h"
#include "launcher_ui.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
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
struct report_server
{
    int listener;
    pthread_t thread;
    int stop;
    char archive[512], route[64];
};

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

static int send_all( int socket, const void *data, size_t size, const int *stop )
{
    const char *bytes = data;
    while (size)
    {
        int flags = 0;
        if (stop && __atomic_load_n( stop, __ATOMIC_RELAXED )) return 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        int sent = send( socket, bytes, size, flags );
        if (sent <= 0) return 0;
        bytes += sent;
        size -= sent;
    }
    return 1;
}

static void *serve_report( void *opaque )
{
    struct report_server *server = opaque;
    while (!__atomic_load_n( &server->stop, __ATOMIC_RELAXED ))
    {
        struct timeval wait = { 0, 200000 }, timeout = { 3, 0 };
        fd_set readers;
        char request[2048], method[8], route[256], protocol[16], header[512], buffer[16384];
        struct stat st;
        FILE *file;
        int client, bytes, complete = 0;
        struct timespec deadline, current;
        size_t received = 0, remaining;
        FD_ZERO( &readers );
        FD_SET( server->listener, &readers );
        if (select( server->listener + 1, &readers, NULL, NULL, &wait ) <= 0) continue;
        if ((client = accept( server->listener, NULL, NULL )) < 0) continue;
        clock_gettime( CLOCK_MONOTONIC, &deadline );
        deadline.tv_sec += 5;
        setsockopt( client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout) );
        setsockopt( client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout) );
        while (received < sizeof(request) - 1 && !__atomic_load_n( &server->stop, __ATOMIC_RELAXED ))
        {
            clock_gettime( CLOCK_MONOTONIC, &current );
            if (current.tv_sec > deadline.tv_sec ||
                (current.tv_sec == deadline.tv_sec && current.tv_nsec >= deadline.tv_nsec)) break;
            bytes = recv( client, request + received, sizeof(request) - 1 - received, 0 );
            if (bytes <= 0) break;
            received += bytes;
            request[received] = 0;
            if (strstr( request, "\r\n\r\n" )) { complete = 1; break; }
        }
        request[received] = 0;
        if (!complete || sscanf( request, "%7s %255s %15s", method, route, protocol ) != 3 ||
            strcmp( method, "GET" ) || strcmp( route, server->route ) ||
            (strcmp( protocol, "HTTP/1.1" ) && strcmp( protocol, "HTTP/1.0" )))
        {
            const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_all( client, response, strlen(response), &server->stop );
        }
        else if ((file = fopen( server->archive, "rb" )))
        {
            if (!fstat( fileno(file), &st ))
            {
                int length = snprintf( header, sizeof(header),
                    "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: %llu\r\n"
                    "Content-Disposition: attachment; filename=autorun-report.zip\r\n"
                    "Cache-Control: no-store\r\nConnection: close\r\n\r\n", (unsigned long long)st.st_size );
                remaining = st.st_size;
                if (send_all( client, header, length, &server->stop ))
                    while (remaining && !__atomic_load_n( &server->stop, __ATOMIC_RELAXED ))
                    {
                        size_t got = fread( buffer, 1, remaining < sizeof(buffer) ? remaining : sizeof(buffer), file );
                        if (!got || !send_all( client, buffer, got, &server->stop )) break;
                        remaining -= got;
                    }
            }
            fclose( file );
        }
        close( client );
    }
    return NULL;
}

static void share_report( struct ui *ui, const struct wine_nx_launcher_options *options )
{
    struct report_server server = { .listener = -1 };
    struct sockaddr_in address = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY };
    unsigned char token[8];
    char url[160], text[1024], host[INET_ADDRSTRLEN];
    socklen_t address_size = sizeof(address);
    int i;

    ui_progress_begin( ui );
    ui_progress_update( ui, launcher_tr("Diagnostic report"), launcher_tr("Preparing report..."), 0, 0 );
    i = diagnostics_report( options, server.archive, sizeof(server.archive) );
    ui_progress_end( ui );
    if (!i) { ui_message( ui, "Diagnostics", "Report could not be created." ); return; }
    if (!random_bytes( token, sizeof(token) )) goto failed;
    for (i = 0; i < (int)sizeof(token); i++) snprintf( server.route + 1 + i * 2, 3, "%02x", token[i] );
    server.route[0] = '/';
    strcat( server.route, "/report.zip" );
    if ((server.listener = socket( AF_INET, SOCK_STREAM, 0 )) < 0 ||
        bind( server.listener, (struct sockaddr *)&address, sizeof(address) ) ||
        listen( server.listener, 2 ) || getsockname( server.listener, (struct sockaddr *)&address, &address_size )) goto failed;
    {
        struct in_addr ip = { .s_addr = gethostid() };
        if (!ip.s_addr || !inet_ntop( AF_INET, &ip, host, sizeof(host) )) goto failed;
    }
    if (pthread_create( &server.thread, NULL, serve_report, &server )) goto failed;
    snprintf( url, sizeof(url), "http://%s:%u%s", host, ntohs(address.sin_port), server.route );
    snprintf( text, sizeof(text), "%s\n\n%s\n\n%s",
              url, launcher_tr("Connect your PC to the same local network. Keep this screen open. Press B to stop sharing."),
              launcher_tr("The report may contain game paths and diagnostic output. Share it only with people you trust.") );
    ui_confirm( ui, launcher_tr("Download report over Wi-Fi"), text, launcher_tr("Stop sharing") );
    __atomic_store_n( &server.stop, 1, __ATOMIC_RELAXED );
    pthread_join( server.thread, NULL );
    close( server.listener );
    return;
failed:
    if (server.listener >= 0) close( server.listener );
    ui_message( ui, "Diagnostics", "Could not start the download server." );
}

void diagnostics_screen( struct ui *ui, struct wine_nx_launcher_options *options )
{
    struct ui_list list = {0};
    struct ui_row rows[3];
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
        snprintf( rows[2].label, sizeof(rows[2].label), "%s", launcher_tr("Download report over Wi-Fi") );
        rows[2].kind = UI_ROW_ACTION;
        rows[2].help = "Create a ZIP with logs and diagnostic settings. No game files, saves or raw settings are included.";
        action = ui_list_run( ui, &list, "Diagnostics", NULL, rows, 3, 0 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) return;
        if (list.selection == 0) options->verbose = !options->verbose;
        else if (list.selection == 1) options->profile = !options->profile;
        else if (action == UI_ACTION_CHOOSE) { share_report( ui, options ); ui_start_screen( ui ); }
    }
}
