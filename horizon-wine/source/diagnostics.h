#ifndef AUTORUN_DIAGNOSTICS_H
#define AUTORUN_DIAGNOSTICS_H

#include <stddef.h>
struct ui;
struct wine_nx_launcher_options;
int diagnostics_report( const struct wine_nx_launcher_options *options, char *path, size_t size );
int diagnostics_screen( struct ui *ui, struct wine_nx_launcher_options *options );

#endif
