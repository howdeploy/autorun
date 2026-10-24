#include "ftp_server.h"
#include "ftp_server_vfs.h"
#include "ftpsrv.h"
#include "launcher.h"
#include "launcher_i18n.h"
#include "launcher_settings.h"
#include "launcher_ui.h"
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

struct ftp_worker { int stop, failed; pthread_t thread; };

static void *ftp_loop( void *opaque )
{
    struct ftp_worker *worker = opaque;
    while (!__atomic_load_n( &worker->stop, __ATOMIC_RELAXED ))
        if (ftpsrv_loop( 100 ) != FTP_API_LOOP_ERROR_OK)
        {
            __atomic_store_n( &worker->failed, 1, __ATOMIC_RELAXED );
            break;
        }
    return NULL;
}

static int valid_account( const char *text )
{
    size_t size = strlen( text );
    if (!size || size >= 128 || text[0] == ' ' || text[size - 1] == ' ') return 0;
    for (size_t i = 0; i < size; i++) if ((unsigned char)text[i] < 32 || text[i] == 127) return 0;
    return 1;
}

static int share_files( struct ui *ui, const struct wine_nx_launcher_options *options,
                        const char *user, const char *password, unsigned port )
{
    struct FtpSrvConfig config = { .port = port, .timeout = 300, .use_localtime = true };
    struct ftp_worker worker = {0};
    struct in_addr ip = { .s_addr = gethostid() };
    char address[INET_ADDRSTRLEN], message[1200];

    if (!ip.s_addr || !inet_ntop( AF_INET, &ip, address, sizeof(address) ) ||
        !autorun_ftp_set_root( options->runtime_dir ))
    { ui_message( ui, "FTP server", "The local network or runtime folder is unavailable." ); return 0; }
    snprintf( config.user, sizeof(config.user), "%s", user );
    snprintf( config.pass, sizeof(config.pass), "%s", password );
    if (ftpsrv_init( &config ) < 0)
    {
        ftpsrv_exit();
        memset( &config, 0, sizeof(config) );
        ui_message( ui, "FTP server", "Could not start FTP. Check the port and network connection." );
        return 0;
    }
    memset( &config, 0, sizeof(config) );
    if (pthread_create( &worker.thread, NULL, ftp_loop, &worker ))
    { ftpsrv_exit(); ui_message( ui, "FTP server", "Could not start the FTP worker." ); return 0; }
    snprintf( message, sizeof(message), "%s:%u\n%s: %s\n\n%s\n\n%s",
              address, port, launcher_tr("Username"), user,
              launcher_tr("FTP / is switch/wine. Games: /drive_c; logs: /logs; reports: /reports."),
              launcher_tr("Use your saved password and passive FTP mode. Keep this screen open until transfers finish. B stops FTP.") );
    ui_confirm( ui, launcher_tr("FTP server running"), message, launcher_tr("Stop FTP") );
    __atomic_store_n( &worker.stop, 1, __ATOMIC_RELAXED );
    pthread_join( worker.thread, NULL );
    ftpsrv_exit();
    if (__atomic_load_n( &worker.failed, __ATOMIC_RELAXED ))
        ui_message( ui, "FTP server", "FTP stopped because of a network error." );
    return 1;
}

int ftp_server_screen( struct ui *ui, struct wine_nx_launcher_options *options )
{
    struct launcher_kv settings;
    struct ui_list list = {0};
    struct ui_row rows[4];
    char path[768], user[128] = "autorun", password[128] = "", value[128];
    unsigned port = 5000;
    int served = 0;

    snprintf( path, sizeof(path), "%s/config/ftp.txt", options->runtime_dir );
    if (!launcher_kv_load( &settings, path ))
    { ui_message( ui, "FTP server", "The FTP settings file could not be read." ); return 0; }
    launcher_kv_get( &settings, "user", user, sizeof(user) );
    launcher_kv_get( &settings, "password", password, sizeof(password) );
    if (launcher_kv_get( &settings, "port", value, sizeof(value) ))
    {
        char *end;
        unsigned long saved = strtoul( value, &end, 10 );
        if (*value && !*end && saved >= 1024 && saved <= 65535) port = saved;
    }
    for (;;)
    {
        enum ui_action action;
        memset( rows, 0, sizeof(rows) );
        const char *labels[] = { "Start FTP", "Port", "Username", "Password" };
        for (int i = 0; i < 4; i++)
        {
            snprintf( rows[i].label, sizeof(rows[i].label), "%s", launcher_tr(labels[i]) );
            rows[i].kind = UI_ROW_ACTION;
        }
        rows[0].disabled = !valid_account( user ) || !valid_account( password );
        rows[0].help = "Upload games and settings, or download logs. The server is limited to switch/wine.";
        snprintf( rows[1].value, sizeof(rows[1].value), "%u", port );
        snprintf( rows[2].value, sizeof(rows[2].value), "%s", user );
        rows[2].literal = 1;
        snprintf( rows[3].value, sizeof(rows[3].value), "%s", launcher_tr(password[0] ? "Configured" : "Not set") );
        rows[3].help = "Set once; saved for the next session. Anonymous access is disabled.";
        action = ui_list_run( ui, &list, "FTP server", NULL, rows, 4, 0 );
        if (action == UI_ACTION_BACK || action == UI_ACTION_QUIT) break;
        if (action != UI_ACTION_CHOOSE) continue;
        if (!list.selection)
        {
            if (options->sync_settings && !options->sync_settings( options, 1 ))
            { ui_message( ui, "FTP server", "Settings could not be saved before the transfer." ); continue; }
            served |= share_files( ui, options, user, password, port );
            if (options->sync_settings && !options->sync_settings( options, 0 ))
                ui_message( ui, "FTP server", "Runtime settings could not be reloaded after the transfer." );
            break;
        }
        value[0] = 0;
        if (list.selection == 1) snprintf( value, sizeof(value), "%u", port );
        else if (list.selection == 2) snprintf( value, sizeof(value), "%s", user );
        if (!launcher_platform_prompt( launcher_tr(labels[list.selection]), value, value, sizeof(value) )) continue;
        if (list.selection == 1)
        {
            char *end;
            unsigned long selected = strtoul( value, &end, 10 );
            if (!*value || *end || selected < 1024 || selected > 65535)
            { ui_message( ui, "FTP server", "Choose a port from 1024 to 65535." ); continue; }
            port = selected;
        }
        else
        {
            if (!valid_account( value ))
            { ui_message( ui, "FTP server", "Enter a nonempty username or password without control characters or edge spaces." ); continue; }
            snprintf( list.selection == 2 ? user : password, sizeof(user), "%s", value );
        }
        launcher_kv_set( &settings, "user", user );
        launcher_kv_set( &settings, "password", password );
        snprintf( value, sizeof(value), "%u", port );
        launcher_kv_set( &settings, "port", value );
        if (!launcher_kv_save( &settings, path ))
            ui_message( ui, "FTP server", "The FTP settings file could not be saved." );
    }
    memset( password, 0, sizeof(password) );
    memset( &settings, 0, sizeof(settings) );
    return served;
}
