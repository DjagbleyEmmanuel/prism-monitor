#ifndef PRISM_I18N_H
#define PRISM_I18N_H

/* Minimal gettext integration.
 * Build with -DENABLE_NLS and link libintl to enable translations.
 * Without it, _() is identity — the app remains fully English. */
#ifdef ENABLE_NLS
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) String
#endif

#ifdef ENABLE_NLS
#define prism_i18n_init() do { \
    bindtextdomain("prism-monitor", "/usr/share/locale"); \
    textdomain("prism-monitor"); \
} while (0)
#else
#define prism_i18n_init() do { } while (0)
#endif

#endif
