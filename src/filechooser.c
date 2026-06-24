#define _GNU_SOURCE 1

#include <config.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "xdg-desktop-portal-dbus.h"
#include "filechooser.h"
#include "request.h"
#include "utils.h"

typedef enum {
    FILE_CHOOSER_ACTION_OPEN,
    FILE_CHOOSER_ACTION_SAVE,
    FILE_CHOOSER_ACTION_SAVE_FILES
} FileChooserActionType;

typedef struct {
    XdpImplFileChooser *impl;
    GDBusMethodInvocation *invocation;
    Request *request;
    FileChooserActionType action_type;
    GVariant *options;
    GtkWidget *dialog;
} FileChooserHandle;

static void on_dialog_response (GtkDialog *dialog, gint response_id, gpointer user_data);
static gboolean handle_close (XdpImplRequest *object, GDBusMethodInvocation *invocation, gpointer user_data);

static void
file_chooser_handle_free (FileChooserHandle *handle)
{
    g_clear_object (&handle->request);
    if (handle->options)
        g_variant_unref (handle->options);
    g_free (handle);
}

static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              gpointer user_data)
{
    FileChooserHandle *handle = user_data;
    g_debug ("FileChooser request closed by client (handle-close)");

    if (handle->dialog) {
        g_signal_handlers_disconnect_by_func (handle->dialog, G_CALLBACK (on_dialog_response), handle);
        gtk_widget_destroy (handle->dialog);
        handle->dialog = NULL;
    }

    g_autoptr(GVariantBuilder) results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
    
    if (handle->action_type == FILE_CHOOSER_ACTION_OPEN) {
        xdp_impl_file_chooser_complete_open_file (handle->impl, handle->invocation, 1, g_variant_builder_end (results_builder));
    } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE) {
        xdp_impl_file_chooser_complete_save_file (handle->impl, handle->invocation, 1, g_variant_builder_end (results_builder));
    } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE_FILES) {
        xdp_impl_file_chooser_complete_save_files (handle->impl, handle->invocation, 1, g_variant_builder_end (results_builder));
    }

    file_chooser_handle_free (handle);
    return FALSE;
}

static void
on_dialog_response (GtkDialog *dialog,
                    gint response_id,
                    gpointer user_data)
{
    FileChooserHandle *handle = user_data;
    guint response = 2; // Default to error
    g_autoptr(GVariantBuilder) results_builder = NULL;

    results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
    g_debug ("on_dialog_response: response_id=%d", response_id);

    g_signal_handlers_disconnect_by_func (handle->request, G_CALLBACK (handle_close), handle);

    if (response_id == GTK_RESPONSE_ACCEPT) {
        if (handle->action_type == FILE_CHOOSER_ACTION_OPEN) {
            GSList *filenames = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (dialog));
            if (filenames) {
                response = 0; // Success
                g_autoptr(GVariantBuilder) uris_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
                for (GSList *l = filenames; l != NULL; l = l->next) {
                    gchar *filename = l->data;
                    gchar *uri = g_filename_to_uri (filename, NULL, NULL);
                    if (uri) {
                        g_variant_builder_add (uris_builder, "s", uri);
                        g_free (uri);
                    }
                    g_free (filename);
                }
                g_slist_free (filenames);
                g_variant_builder_add (results_builder, "{sv}", "uris", g_variant_builder_end (uris_builder));
            } else {
                response = 1; // Cancelled
            }
        } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE) {
            gchar *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
            if (filename) {
                response = 0; // Success
                gchar *uri = g_filename_to_uri (filename, NULL, NULL);
                if (uri) {
                    const gchar *uris[2] = { uri, NULL };
                    g_variant_builder_add (results_builder, "{sv}", "uris", g_variant_new_strv (uris, -1));
                    g_free (uri);
                }
                g_free (filename);
            } else {
                response = 1; // Cancelled
            }
        } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE_FILES) {
            GSList *filenames_selected = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (dialog));
            if (filenames_selected) {
                response = 0; // Success
                g_autoptr(GVariantBuilder) uris_builder = g_variant_builder_new (G_VARIANT_TYPE ("as"));
                gchar *selected_dir = filenames_selected->data;
                
                g_auto(GStrv) suggested_filenames = NULL;
                if (g_variant_lookup (handle->options, "filenames", "^as", &suggested_filenames)) {
                    for (gint i = 0; suggested_filenames[i] != NULL; i++) {
                        gchar *full_path = g_build_filename (selected_dir, suggested_filenames[i], NULL);
                        gchar *uri = g_filename_to_uri (full_path, NULL, NULL);
                        if (uri) {
                            g_variant_builder_add (uris_builder, "s", uri);
                            g_free (uri);
                        }
                        g_free (full_path);
                    }
                } else {
                    gchar *uri = g_filename_to_uri (selected_dir, NULL, NULL);
                    if (uri) {
                        g_variant_builder_add (uris_builder, "s", uri);
                        g_free (uri);
                    }
                }
                
                g_variant_builder_add (results_builder, "{sv}", "uris", g_variant_builder_end (uris_builder));
                g_slist_free_full (filenames_selected, g_free);
            } else {
                response = 1; // Cancelled
            }
        }
    } else if (response_id == GTK_RESPONSE_CANCEL || response_id == GTK_RESPONSE_DELETE_EVENT) {
        response = 1; // Cancelled
    } else {
        response = 2; // Error
    }

    if (handle->request->exported) {
        request_unexport (handle->request);
    }

    g_debug ("on_dialog_response: completing call with response %u", response);

    if (handle->action_type == FILE_CHOOSER_ACTION_OPEN) {
        xdp_impl_file_chooser_complete_open_file (handle->impl,
                                                 handle->invocation,
                                                 response,
                                                 g_variant_builder_end (results_builder));
    } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE) {
        xdp_impl_file_chooser_complete_save_file (handle->impl,
                                                 handle->invocation,
                                                 response,
                                                 g_variant_builder_end (results_builder));
    } else if (handle->action_type == FILE_CHOOSER_ACTION_SAVE_FILES) {
        xdp_impl_file_chooser_complete_save_files (handle->impl,
                                                  handle->invocation,
                                                  response,
                                                  g_variant_builder_end (results_builder));
    }

    gtk_widget_destroy (GTK_WIDGET (dialog));
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

    g_debug ("handle_open_file: started");

    if (!gtk_init_check (NULL, NULL)) {
        g_warning ("GTK is not initialized or display is not available");
        g_autoptr(GVariantBuilder) results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
        xdp_impl_file_chooser_complete_open_file (object, invocation, 2, g_variant_builder_end (results_builder));
        return TRUE;
    }

    sender = g_dbus_method_invocation_get_sender (invocation);
    connection = g_dbus_method_invocation_get_connection (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    gboolean multiple = FALSE;
    g_variant_lookup (arg_options, "multiple", "b", &multiple);
    gboolean directory = FALSE;
    g_variant_lookup (arg_options, "directory", "b", &directory);

    g_debug ("handle_open_file: multiple=%d, directory=%d", multiple, directory);

    GtkWidget *dialog = gtk_file_chooser_dialog_new (arg_title ? arg_title : (directory ? _("Select Folder") : _("Open File")),
                                                     NULL,
                                                     directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
                                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                     _("_Open"), GTK_RESPONSE_ACCEPT,
                                                     NULL);

    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), multiple);

    const gchar *current_folder_path = NULL;
    if (g_variant_lookup (arg_options, "current_folder", "^ay", &current_folder_path)) {
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), current_folder_path);
    }

    const gchar *current_file_path = NULL;
    if (g_variant_lookup (arg_options, "current_file", "^ay", &current_file_path)) {
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (dialog), current_file_path);
    }

    GVariantIter *filters_iter = NULL;
    if (g_variant_lookup (arg_options, "filters", "a(sa(us))", &filters_iter)) {
        GVariantIter *filter_iter;
        const gchar *filter_name;
        while (g_variant_iter_loop (filters_iter, "(&sa(us))", &filter_name, &filter_iter)) {
            GtkFileFilter *filter = gtk_file_filter_new ();
            if (filter_name && *filter_name) {
                gtk_file_filter_set_name (filter, filter_name);
            }
            guint32 type;
            const gchar *pattern;
            while (g_variant_iter_loop (filter_iter, "(u&s)", &type, &pattern)) {
                if (type == 0) {
                    gtk_file_filter_add_pattern (filter, pattern);
                } else if (type == 1) {
                    gtk_file_filter_add_mime_type (filter, pattern);
                }
            }
            gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
        }
        g_variant_iter_free (filters_iter);
    }

    handle = g_new0 (FileChooserHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);
    handle->action_type = FILE_CHOOSER_ACTION_OPEN;
    handle->options = g_variant_ref (arg_options);
    handle->dialog = dialog;

    g_signal_connect (dialog, "response", G_CALLBACK (on_dialog_response), handle);
    g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

    request_export (request, connection);

    gtk_widget_show_all (dialog);
    gtk_window_present (GTK_WINDOW (dialog));

    return TRUE;
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

    g_debug ("handle_save_file: started");

    if (!gtk_init_check (NULL, NULL)) {
        g_warning ("GTK is not initialized or display is not available");
        g_autoptr(GVariantBuilder) results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
        xdp_impl_file_chooser_complete_save_file (object, invocation, 2, g_variant_builder_end (results_builder));
        return TRUE;
    }

    sender = g_dbus_method_invocation_get_sender (invocation);
    connection = g_dbus_method_invocation_get_connection (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    GtkWidget *dialog = gtk_file_chooser_dialog_new (arg_title ? arg_title : _("Save File"),
                                                     NULL,
                                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                     _("_Save"), GTK_RESPONSE_ACCEPT,
                                                     NULL);

    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

    const gchar *current_folder_path = NULL;
    if (g_variant_lookup (arg_options, "current_folder", "^ay", &current_folder_path)) {
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), current_folder_path);
    }

    const gchar *current_name = NULL;
    if (g_variant_lookup (arg_options, "current_name", "&s", &current_name)) {
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), current_name);
    }

    const gchar *current_file_path = NULL;
    if (g_variant_lookup (arg_options, "current_file", "^ay", &current_file_path)) {
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (dialog), current_file_path);
    }

    GVariantIter *filters_iter = NULL;
    if (g_variant_lookup (arg_options, "filters", "a(sa(us))", &filters_iter)) {
        GVariantIter *filter_iter;
        const gchar *filter_name;
        while (g_variant_iter_loop (filters_iter, "(&sa(us))", &filter_name, &filter_iter)) {
            GtkFileFilter *filter = gtk_file_filter_new ();
            if (filter_name && *filter_name) {
                gtk_file_filter_set_name (filter, filter_name);
            }
            guint32 type;
            const gchar *pattern;
            while (g_variant_iter_loop (filter_iter, "(u&s)", &type, &pattern)) {
                if (type == 0) {
                    gtk_file_filter_add_pattern (filter, pattern);
                } else if (type == 1) {
                    gtk_file_filter_add_mime_type (filter, pattern);
                }
            }
            gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);
        }
        g_variant_iter_free (filters_iter);
    }

    handle = g_new0 (FileChooserHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);
    handle->action_type = FILE_CHOOSER_ACTION_SAVE;
    handle->options = g_variant_ref (arg_options);
    handle->dialog = dialog;

    g_signal_connect (dialog, "response", G_CALLBACK (on_dialog_response), handle);
    g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

    request_export (request, connection);

    gtk_widget_show_all (dialog);
    gtk_window_present (GTK_WINDOW (dialog));

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
    GDBusConnection *connection;
    FileChooserHandle *handle;
    g_autoptr(Request) request = NULL;
    const char *sender;

    g_debug ("handle_save_files: started");

    if (!gtk_init_check (NULL, NULL)) {
        g_warning ("GTK is not initialized or display is not available");
        g_autoptr(GVariantBuilder) results_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
        xdp_impl_file_chooser_complete_save_files (object, invocation, 2, g_variant_builder_end (results_builder));
        return TRUE;
    }

    sender = g_dbus_method_invocation_get_sender (invocation);
    connection = g_dbus_method_invocation_get_connection (invocation);
    request = request_new (sender, arg_app_id, arg_handle);

    GtkWidget *dialog = gtk_file_chooser_dialog_new (arg_title ? arg_title : _("Select Folder to Save Files"),
                                                     NULL,
                                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
                                                     _("_Save"), GTK_RESPONSE_ACCEPT,
                                                     NULL);

    const gchar *current_folder_path = NULL;
    if (g_variant_lookup (arg_options, "current_folder", "^ay", &current_folder_path)) {
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), current_folder_path);
    }

    handle = g_new0 (FileChooserHandle, 1);
    handle->impl = object;
    handle->invocation = invocation;
    handle->request = g_object_ref (request);
    handle->action_type = FILE_CHOOSER_ACTION_SAVE_FILES;
    handle->options = g_variant_ref (arg_options);
    handle->dialog = dialog;

    g_signal_connect (dialog, "response", G_CALLBACK (on_dialog_response), handle);
    g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

    request_export (request, connection);

    gtk_widget_show_all (dialog);
    gtk_window_present (GTK_WINDOW (dialog));

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
