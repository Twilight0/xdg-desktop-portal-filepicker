#define _GNU_SOURCE 1

#include <config.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"
#include "filechooser.h"
#include "request.h"
#include "utils.h"

typedef struct {
    XdpImplFileChooser *impl;
    GDBusMethodInvocation *invocation;
    Request *request;
} FileChooserHandle;

static void
file_chooser_handle_free (FileChooserHandle *handle)
{
    g_clear_object (&handle->request);
    g_free (handle);
}

static void
on_nemo_open_file_cb (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
    FileChooserHandle *handle = user_data;
    g_autoptr(GVariant) reply = NULL;
    g_autoptr(GError) error = NULL;
    guint response = 2; // Default to error
    g_autoptr(GVariantBuilder) results_builder = NULL;

    results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

    reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);

    if (error) {
        g_warning ("Failed to call Nemo OpenFile: %s", error->message);
        response = 2;
    } else {
        g_auto(GStrv) uris = NULL;
        g_variant_get (reply, "(^as)", &uris);

        g_debug ("on_nemo_open_file_cb: received %d URIs from Nemo", uris ? (gint)g_strv_length (uris) : 0);
        if (uris) {
            for (gint i = 0; uris[i] != NULL; i++) {
                g_debug ("on_nemo_open_file_cb: URI[%d]: %s", i, uris[i]);
            }
        }

        if (uris && g_strv_length (uris) > 0) {
            response = 0; // Success
            g_variant_builder_add (results_builder, "{sv}", "uris", g_variant_new_strv ((const gchar * const *)uris, -1));
        } else {
            response = 1; // Cancelled
        }
    }

    if (handle->request->exported) {
        request_unexport (handle->request);
    }

    g_debug ("on_nemo_open_file_cb: completing OpenFile with response %u", response);

    xdp_impl_file_chooser_complete_open_file (handle->impl,
                                             handle->invocation,
                                             response,
                                             g_variant_builder_end (results_builder));

    file_chooser_handle_free (handle);
}

static gboolean
handle_open_file (XdpImplFileChooser *object,
                  GDBusMethodInvocation *invocation,
                  const char *arg_handle,
                  const char *arg_app_id,
                  const char *arg_parent_window,
                  const char *arg_title,
                  GVariant *arg_options)
{
    GDBusConnection *connection;
    FileChooserHandle *handle;
    g_autoptr(Request) request = NULL;
    const char *sender;

    g_debug ("Got new FileChooser OpenFile request");

    sender = g_dbus_method_invocation_get_sender (invocation);
    connection = g_dbus_method_invocation_get_connection (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    handle = g_new0 (FileChooserHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);

    // Unpack options
    gboolean multiple = FALSE;
    g_variant_lookup (arg_options, "multiple", "b", &multiple);
    gboolean directory = FALSE;
    g_variant_lookup (arg_options, "directory", "b", &directory);

    g_debug ("handle_open_file: multiple=%d, directory=%d", multiple, directory);

    g_autofree gchar *current_folder_uri = NULL;
    const gchar *current_folder_path = NULL;
    if (g_variant_lookup (arg_options, "current_folder", "^ay", &current_folder_path)) {
        g_autoptr(GFile) f = g_file_new_for_path (current_folder_path);
        current_folder_uri = g_file_get_uri (f);
    }

    // Call Nemo D-Bus asynchronously
    g_autoptr(GVariantBuilder) filters_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
    // Add empty filters for now
    
    g_dbus_connection_call (connection,
                            "org.Nemo.FileChooser",
                            "/org/Nemo/FileChooser",
                            "org.Nemo.FileChooser",
                            "OpenFile",
                            g_variant_new ("(sasbbs)",
                                           arg_title ? arg_title : "",
                                           filters_builder,
                                           multiple,
                                           directory,
                                           current_folder_uri ? current_folder_uri : ""),
                            G_VARIANT_TYPE ("(as)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            on_nemo_open_file_cb,
                            handle);

    request_export (request, connection);
    return TRUE;
}

static void
on_nemo_save_file_cb (GObject *source_object,
                      GAsyncResult *res,
                      gpointer user_data)
{
    FileChooserHandle *handle = user_data;
    g_autoptr(GVariant) reply = NULL;
    g_autoptr(GError) error = NULL;
    guint response = 2; // Default to error
    g_autoptr(GVariantBuilder) results_builder = NULL;

    results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

    reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source_object), res, &error);

    if (error) {
        g_warning ("Failed to call Nemo SaveFile: %s", error->message);
        response = 2;
    } else {
        const gchar *result_uri = NULL;
        g_variant_get (reply, "(&s)", &result_uri);

        if (result_uri && *result_uri) {
            response = 0; // Success
            const gchar *uris[2] = { result_uri, NULL };
            g_variant_builder_add (results_builder, "{sv}", "uris", g_variant_new_strv (uris, -1));
        } else {
            response = 1; // Cancelled
        }
    }

    if (handle->request->exported) {
        request_unexport (handle->request);
    }

    xdp_impl_file_chooser_complete_save_file (handle->impl,
                                             handle->invocation,
                                             response,
                                             g_variant_builder_end (results_builder));

    file_chooser_handle_free (handle);
}

static gboolean
handle_save_file (XdpImplFileChooser *object,
                  GDBusMethodInvocation *invocation,
                  const char *arg_handle,
                  const char *arg_app_id,
                  const char *arg_parent_window,
                  const char *arg_title,
                  GVariant *arg_options)
{
    GDBusConnection *connection;
    FileChooserHandle *handle;
    g_autoptr(Request) request = NULL;
    const char *sender;

    g_debug ("Got new FileChooser SaveFile request");

    sender = g_dbus_method_invocation_get_sender (invocation);
    connection = g_dbus_method_invocation_get_connection (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    handle = g_new0 (FileChooserHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);

    // Unpack options
    g_autofree gchar *current_folder_uri = NULL;
    const gchar *current_folder_path = NULL;
    if (g_variant_lookup (arg_options, "current_folder", "^ay", &current_folder_path)) {
        g_autoptr(GFile) f = g_file_new_for_path (current_folder_path);
        current_folder_uri = g_file_get_uri (f);
    }

    const gchar *suggested_name = NULL;
    g_variant_lookup (arg_options, "current_name", "&s", &suggested_name);

    g_dbus_connection_call (connection,
                            "org.Nemo.FileChooser",
                            "/org/Nemo/FileChooser",
                            "org.Nemo.FileChooser",
                            "SaveFile",
                            g_variant_new ("(sss)",
                                           arg_title ? arg_title : "",
                                           current_folder_uri ? current_folder_uri : "",
                                           suggested_name ? suggested_name : ""),
                            G_VARIANT_TYPE ("(s)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            on_nemo_save_file_cb,
                            handle);

    request_export (request, connection);
    return TRUE;
}

static gboolean
handle_save_files (XdpImplFileChooser *object,
                   GDBusMethodInvocation *invocation,
                   const char *arg_handle,
                   const char *arg_app_id,
                   const char *arg_parent_window,
                   const char *arg_title,
                   GVariant *arg_options)
{
    // SaveFiles is very similar to OpenFile in this proxy context.
    // For baseline, complete it with cancellation since it's rarely used.
    g_autoptr(GVariantBuilder) results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
    xdp_impl_file_chooser_complete_save_files (object, invocation, 1, g_variant_builder_end (results_builder));
    return TRUE;
}

gboolean
filechooser_init (GDBusConnection *bus,
                  GError **error)
{
    GDBusInterfaceSkeleton *helper;

    helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_file_chooser_skeleton_new ());

    g_signal_connect (helper, "handle-open-file", G_CALLBACK (handle_open_file), NULL);
    g_signal_connect (helper, "handle-save-file", G_CALLBACK (handle_save_file), NULL);
    g_signal_connect (helper, "handle-save-files", G_CALLBACK (handle_save_files), NULL);

    if (!g_dbus_interface_skeleton_export (helper,
                                           bus,
                                           DESKTOP_PORTAL_OBJECT_PATH,
                                           error)) {
        return FALSE;
    }

    g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

    return TRUE;
}
