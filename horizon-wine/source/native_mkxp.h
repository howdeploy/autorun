#ifndef AUTORUN_NATIVE_MKXP_H
#define AUTORUN_NATIVE_MKXP_H

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define NATIVE_MKXP_MACHINE 0xfffe
#define NATIVE_MKXP_NRO "sdmc:/switch/wine/engines/mkxp-z/mkxp-z.nro"
#define NATIVE_MKXP_AUTORUN "sdmc:/switch/wine/wine-nx-runtime.nro"

static inline int native_mkxp_name( const char *path )
{
    const char *name = strrchr( path, '/' );
    size_t len;

    name = name ? name + 1 : path;
    len = strlen( name );
    return !strcasecmp( name, "Game.ini" ) || (len > 6 && !strcasecmp( name + len - 6, ".mkxpz" ));
}

/* The loader parses a quoted command line, rather than an argv array. */
static inline int native_mkxp_path( const char *path )
{
    const unsigned char *p = (const unsigned char *)path;
    if (strncmp( path, "sdmc:/", 6 ) || strlen( path ) >= 512) return 0;
    for (; *p; p++) if (*p < 32 || *p == 127 || *p == '"' || *p == '\\') return 0;
    return 1;
}

/* Recognize RGSS content without interpreting a Game.ini as a Windows PE. */
static inline int native_mkxp_describe( const char *path, char *title, size_t size )
{
    struct stat st;
    FILE *file;
    char line[1024];
    int in_game = 0, scripts = 0;
    size_t total = 0;
    const char *name = strrchr( path, '/' );

    if (title && size) title[0] = 0;
    if (!native_mkxp_name( path ) || !native_mkxp_path( path ) || stat( path, &st ) ||
        !S_ISREG( st.st_mode ) || st.st_size <= 0 || !(file = fopen( path, "rb" ))) return 0;
    name = name ? name + 1 : path;
    if (strcasecmp( name, "Game.ini" ))
    {
        unsigned char magic[6];
        size_t got = fread( magic, 1, sizeof(magic), file );
        int archive = got == sizeof(magic) &&
                      (!memcmp( magic, "PK\003\004", 4 ) || !memcmp( magic, "7z\274\257\047\034", 6 ));
        fclose( file );
        if (archive && title && size) snprintf( title, size, "%.*s", (int)strlen( name ) - 6, name );
        return archive;
    }
    while (total < 65536 && fgets( line, sizeof(line), file ))
    {
        char *key = line, *end, *value;
        size_t len = strlen( line );
        if (!total && len >= 3 && !memcmp( key, "\357\273\277", 3 )) key += 3;
        total += len;
        while (isspace( (unsigned char)*key )) key++;
        end = key + strlen( key );
        while (end > key && isspace( (unsigned char)end[-1] )) *--end = 0;
        if (*key == '[') { in_game = !strcasecmp( key, "[Game]" ); continue; }
        if (!in_game || *key == ';' || *key == '#' || !(value = strchr( key, '=' ))) continue;
        end = value++;
        while (end > key && isspace( (unsigned char)end[-1] )) end--;
        *end = 0;
        while (isspace( (unsigned char)*value )) value++;
        if (!strcasecmp( key, "Title" ) && title && size) snprintf( title, size, "%s", value );
        if (!strcasecmp( key, "Scripts" ) && (end = strrchr( value, '.' )))
            scripts = !strcasecmp( end, ".rxdata" ) || !strcasecmp( end, ".rvdata" ) || !strcasecmp( end, ".rvdata2" );
    }
    fclose( file );
    return scripts;
}

static inline int native_mkxp_module_ready( void )
{
    unsigned char head[32];
    FILE *file = fopen( NATIVE_MKXP_NRO, "rb" );
    size_t got;
    if (!file) return 0;
    got = fread( head, 1, sizeof(head), file );
    fclose( file );
    return got == sizeof(head) && !memcmp( head + 16, "NRO0", 4 );
}

static inline int native_mkxp_arguments( const char *path, char *out, size_t size )
{
    int len;
    if (!native_mkxp_path( path ) || !native_mkxp_name( path )) return 0;
    len = snprintf( out, size, "\"%s\" --autorun \"%s\"", NATIVE_MKXP_NRO, path );
    return len > 0 && (size_t)len < size && len < 2048;
}

#endif
