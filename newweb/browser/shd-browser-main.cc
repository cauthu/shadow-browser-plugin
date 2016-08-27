#include <glib.h>
#include <shd-library.h>
#include "browser.hpp"
#include "common.hpp"

ShadowLogFunc logfn;
ShadowCreateCallbackFunc scheduleCallback;

/* my global structure with application state */
browser_t b;

void bmain_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...) {
    va_list vargs;
    va_start(vargs, format);

    GString* newformat = g_string_new(NULL);
    g_string_append_printf(newformat, "[%s] %s", functionName, format);
    g_logv(G_LOG_DOMAIN, level, newformat->str, vargs);
    g_string_free(newformat, TRUE);

    va_end(vargs);
}

void bmain_createCallback(ShadowPluginCallbackFunc callback,
                          gpointer data, guint millisecondsDelay)
{
	sleep(millisecondsDelay / 1000);
	callback(data);
}

gint main(gint argc, gchar *argv[])
{
    logfn = bmain_log;
    scheduleCallback = bmain_createCallback;

    b.start(argc, argv);

    b.activate(true);

    return 0;
}
