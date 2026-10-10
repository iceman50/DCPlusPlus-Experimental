/* Copyright (C) 2026 iceman50 */
/* Preserve the locale helper used by the existing DC++ integration. */
#include "config.h"
#include "localename.h"

const char *_nl_locale_name_default(void) {
    return gl_locale_name_default();
}
