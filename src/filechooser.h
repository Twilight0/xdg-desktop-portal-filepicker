#ifndef __PORTAL_FILECHOOSER_H__
#define __PORTAL_FILECHOOSER_H__

#include <gio/gio.h>

gboolean filechooser_init (GDBusConnection *connection,
                           GError **error);

#endif /* __PORTAL_FILECHOOSER_H__ */
