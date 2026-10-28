/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <switch.h>
#include <stdio.h>
#include <string.h>
#include "native_mkxp.h"

extern int rarch_main( int argc, char *argv[], void *data );

/* --wrap=main bypasses the standalone Lona bootstrap without changing it. */
int __wrap_main( int argc, char *argv[] )
{
    FILE *log = fopen( "sdmc:/switch/wine/mkxp-z-module.log", "a" );
    int result = 1;

    _Static_assert( sizeof(NATIVE_MKXP_NRO) + 512 + sizeof("\"\" --autorun \"\"") < 2048,
                    "mkxp-z handoff must fit the homebrew loader argument buffer" );
    if (argc == 3 && !strcmp( argv[1], "--autorun" ) && native_mkxp_describe( argv[2], NULL, 0 ))
    {
        /* This frontend's realpath treats only slash-prefixed paths as absolute. */
        char *content_argv[] = { "retroarch", argv[2] + 5, NULL };
        Result rc = romfsInit();

        if (log) { fprintf( log, "[MKXP] content=%s romfs=0x%x\n", argv[2], rc ); fflush( log ); }
        if (R_SUCCEEDED( rc ))
        {
            result = rarch_main( 2, content_argv, NULL );
            romfsExit();
        }
    }
    else if (log) fprintf( log, "[MKXP] Invalid invocation or unavailable RGSS content (argc=%d)\n", argc );

    if (envHasNextLoad())
    {
        Result rc = envSetNextLoad( NATIVE_MKXP_AUTORUN, NATIVE_MKXP_AUTORUN );
        if (log) fprintf( log, "[MKXP] exit=%d return-to-Autorun=0x%x\n", result, rc );
    }
    else if (log) fprintf( log, "[MKXP] No loader available to return to Autorun\n" );
    if (log) fclose( log );
    return result;
}
