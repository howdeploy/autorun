#ifndef AUTORUN_LAUNCHER_I18N_H
#define AUTORUN_LAUNCHER_I18N_H

#include <stddef.h>

enum launcher_language { LAUNCHER_ENGLISH, LAUNCHER_RUSSIAN };
void launcher_set_language( const char *language );
const char *launcher_language_code(void);
const char *launcher_tr( const char *text );
int launcher_snprintf( char *out, size_t size, const char *format, ... )
    __attribute__((format(printf, 3, 4)));

#endif
