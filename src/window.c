#include "window.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <locale.h>
#include "application.h"
#include "entrydialog.h"
#include "keydialog.h"
#include "config.h"

#include <gpgme.h>
#include "cryptography.h"
#include "threading.h"

#define ACTION_MODE_TEXT 0
#define ACTION_MODE_FILE 1

#define HANDLE_ERROR_EMAIL(status, key, ui_function, ui_data, memory) if (key == NULL) { \
        \
        gpgme_key_release(key); \
        \
        memory \
        \
        g_idle_add((GSourceFunc) ui_function, ui_data); \
        return status; \
    }

/**
 * This structure handles data of a window.
 */
struct _LockWindow {
    AdwApplicationWindow parent;

    AdwToastOverlay *toast_overlay;

    AdwViewStack *stack;
    unsigned int action_mode;

    gchar *email; /**< Stores the entered email during an encryption process. */

    /* Text */
    AdwViewStackPage *text_page;
    AdwSplitButton *text_button;
    GtkTextBuffer *text_queue; /**< Text from the last cryptography operation on text */
    GtkTextView *text_view;

    /* File */
    AdwViewStackPage *file_page;
    gboolean file_success; /**< Success of the last cryptography operation on files */
    GFile *file_input;
    GFile *file_output;

    AdwActionRow *file_input_row;
    GtkButton *file_input_button;

    AdwActionRow *file_output_row;
    GtkButton *file_output_button;

    GtkButton *file_encrypt_button;
    GtkButton *file_decrypt_button;
    GtkButton *file_sign_button;
    GtkButton *file_verify_button;
};

G_DEFINE_TYPE(LockWindow, lock_window, ADW_TYPE_APPLICATION_WINDOW);

/* UI */
static void lock_window_stack_page_on_changed(AdwViewStack * self,
                                              GParamSpec * pspec,
                                              LockWindow * window);

// Encryption
gboolean lock_window_encrypt_text_on_completed(LockWindow * window);
gboolean lock_window_encrypt_file_on_completed(LockWindow * window);

// Decryption
gboolean lock_window_decrypt_text_on_completed(LockWindow * window);
gboolean lock_window_decrypt_file_on_completed(LockWindow * window);

// Signing
gboolean lock_window_sign_text_on_completed(LockWindow * window);
gboolean lock_window_sign_file_on_completed(LockWindow * window);

// Verification
gboolean lock_window_verify_text_on_completed(LockWindow * window);
gboolean lock_window_verify_file_on_completed(LockWindow * window);

/* Key management */
static void lock_window_key_dialog(GSimpleAction * action, GVariant * parameter,
                                   LockWindow * window);

/* Text */
static void lock_window_text_view_copy(AdwSplitButton * self,
                                       LockWindow * window);
static void lock_window_text_queue_set_text(LockWindow * window,
                                            const char *text);

/* File */
static void lock_window_file_open(GObject * source_object, GAsyncResult * res,
                                  gpointer data);
static void lock_window_file_save(GObject * source_object, GAsyncResult * res,
                                  gpointer data);
static void lock_window_file_open_dialog_present(GtkButton * self,
                                                 LockWindow * window);
static void lock_window_file_save_dialog_present(GtkButton * self,
                                                 LockWindow * window);

/* Encryption */
void lock_window_encrypt_text_dialog(GSimpleAction * self, GVariant * parameter,
                                     LockWindow * window);
void lock_window_encrypt_file_dialog(GtkButton * self, LockWindow * window);

/**
 * This function initializes a LockWindow.
 *
 * @param window Window to be initialized
 */
static void lock_window_init(LockWindow *window)
{
    gtk_widget_init_template(GTK_WIDGET(window));

    window->email = malloc(1024 * sizeof(char));
    strcpy(window->email, "");

    /* Page changed */
    g_signal_connect(window->stack, "notify::visible-child",
                     G_CALLBACK(lock_window_stack_page_on_changed), window);
    lock_window_stack_page_on_changed(window->stack, NULL, window);

    /* Key management */

    g_autoptr(GSimpleAction) manage_keys_action =
        g_simple_action_new("manage_keys", NULL);
    g_signal_connect(manage_keys_action, "activate",
                     G_CALLBACK(lock_window_key_dialog), window);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(manage_keys_action));

    /* Text */
    g_signal_connect(window->text_button, "clicked",
                     G_CALLBACK(lock_window_text_view_copy), window);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(window->text_view),
                             _("Enter text …"), -1);

    window->text_queue = gtk_text_buffer_new(NULL);
    lock_window_text_queue_set_text(window, "");

    // Encrypt
    g_autoptr(GSimpleAction) encrypt_text_action =
        g_simple_action_new("encrypt_text", NULL);
    g_signal_connect(encrypt_text_action, "activate",
                     G_CALLBACK(lock_window_encrypt_text_dialog), window);
    g_action_map_add_action(G_ACTION_MAP(window),
                            G_ACTION(encrypt_text_action));
    // Decrypt
    g_autoptr(GSimpleAction) decrypt_text_action =
        g_simple_action_new("decrypt_text", NULL);
    g_signal_connect(decrypt_text_action, "activate",
                     G_CALLBACK(thread_decrypt_text), window);
    g_action_map_add_action(G_ACTION_MAP(window),
                            G_ACTION(decrypt_text_action));
    // Sign
    g_autoptr(GSimpleAction) sign_text_action =
        g_simple_action_new("sign_text", NULL);
    g_signal_connect(sign_text_action, "activate",
                     G_CALLBACK(thread_sign_text), window);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(sign_text_action));
    // Verify
    g_autoptr(GSimpleAction) verify_text_action =
        g_simple_action_new("verify_text", NULL);
    g_signal_connect(verify_text_action, "activate",
                     G_CALLBACK(thread_verify_text), window);
    g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(verify_text_action));

    /* File */
    g_signal_connect(window->file_input_button, "clicked",
                     G_CALLBACK(lock_window_file_open_dialog_present), window);
    g_signal_connect(window->file_output_button, "clicked",
                     G_CALLBACK(lock_window_file_save_dialog_present), window);
    // Encrypt
    g_signal_connect(window->file_encrypt_button, "clicked",
                     G_CALLBACK(lock_window_encrypt_file_dialog), window);
    // Decrypt
    g_signal_connect(window->file_decrypt_button, "clicked",
                     G_CALLBACK(thread_decrypt_file), window);
    // Sign
    g_signal_connect(window->file_sign_button, "clicked",
                     G_CALLBACK(thread_sign_file), window);
    // Verify
    g_signal_connect(window->file_verify_button, "clicked",
                     G_CALLBACK(thread_verify_file), window);
}

/**
 * This function initializes a LockWindow class.
 *
 * @param class Window class to be initialized
 */
static void lock_window_class_init(LockWindowClass *class)
{
    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class),
                                                UI_RESOURCE("window.ui"));

    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         toast_overlay);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         stack);

    /* Text */
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         text_page);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         text_button);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         text_view);

    /* File */
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_page);

    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_input_row);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_input_button);

    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_output_row);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_output_button);

    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_encrypt_button);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_decrypt_button);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_sign_button);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), LockWindow,
                                         file_verify_button);
}

/**
 * This function creates a new LockWindow.
 *
 * @param app Application to create the new window for
 *
 * @return LockWindow
 */
LockWindow *lock_window_new(LockApplication *app)
{
    return g_object_new(LOCK_TYPE_WINDOW, "application", app, NULL);
}

/**
 * This function opens a LockWindow.
 *
 * @param window Window to be opened
 * @param file File to be processed with the window
 */

void lock_window_open(LockWindow *window, GFile *file)
{
}

/**** UI ****/

/**
 * This function updates the UI on a change of a page of the stack page of a LockWindow.
 *
 * @param self https://docs.gtk.org/gobject/signal.Object.notify.html
 * @param pspec https://docs.gtk.org/gobject/signal.Object.notify.html
 * @param window https://docs.gtk.org/gobject/signal.Object.notify.html
 */
static void lock_window_stack_page_on_changed(AdwViewStack *self,
                                              GParamSpec *pspec,
                                              LockWindow *window)
{
    GtkWidget *visible_child = adw_view_stack_get_visible_child(self);
    AdwViewStackPage *visible_page =
        adw_view_stack_get_page(self, visible_child);

    if (visible_page == window->text_page) {
        window->action_mode = ACTION_MODE_TEXT;

        gtk_widget_set_visible(GTK_WIDGET(window->text_button), true);
    } else if (visible_page == window->file_page) {
        window->action_mode = ACTION_MODE_FILE;

        gtk_widget_set_visible(GTK_WIDGET(window->text_button), false);
    }

    /* Cleanup */
    visible_child = NULL;
    visible_page = NULL;
}

/**** Key management ****/

/**
 * This function initializes the UI required for managing keys.
 *
 * @param self https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 * @param parameter https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 * @param window https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 */
static void lock_window_key_dialog(GSimpleAction *action, GVariant *parameter,
                                   LockWindow *window)
{
    LockKeyDialog *dialog = lock_key_dialog_new();

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
}

/**** Text ****/

/**
 * This function gets the text from the text view of a LockWindow.
 *
 * @param window Window to get the text from
 *
 * @return Text. Freeing required
 */
gchar *lock_window_text_view_get_text(LockWindow *window)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(window->text_view);

    GtkTextIter start_iter;
    GtkTextIter end_iter;
    gtk_text_buffer_get_start_iter(buffer, &start_iter);
    gtk_text_buffer_get_end_iter(buffer, &end_iter);

    return gtk_text_buffer_get_text(buffer, &start_iter, &end_iter, true);
}

/**
 * This functions sets the text of the text view of a LockWindow.
 *
 * @param window Window to set the text in
 * @param text Text to overwrite the text views buffer with
 */
static void lock_window_text_view_set_text(LockWindow *window, const char *text)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(window->text_view);

    gtk_text_buffer_set_text(buffer, text, -1);
}

/**
 * This function copies text from the text view of a LockWindow.
 *
 * @param self https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/signal.SplitButton.clicked.html
 * @param window https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1-latest/signal.SplitButton.clicked.html
 */
static void lock_window_text_view_copy(AdwSplitButton *self, LockWindow *window)
{
    GdkClipboard *active_clipboard =
        gdk_display_get_clipboard(gdk_display_get_default());

    AdwToast *toast = adw_toast_new(_("Text copied"));
    adw_toast_set_timeout(toast, 2);

    gchar *text = lock_window_text_view_get_text(window);
    gdk_clipboard_set_text(active_clipboard, text);

    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Cleanup */
    g_free(text);
    text = NULL;
}

/**
 * This function gets the text from the text queue of a LockWindow.
 *
 * @param window Window to get the text of the queue from
 *
 * @return Text. Freeing required
 */
gchar *lock_window_text_queue_get_text(LockWindow *window)
{
    GtkTextIter start_iter;
    GtkTextIter end_iter;
    gtk_text_buffer_get_start_iter(window->text_queue, &start_iter);
    gtk_text_buffer_get_end_iter(window->text_queue, &end_iter);

    return gtk_text_buffer_get_text(window->text_queue, &start_iter, &end_iter,
                                    true);
}

/**
 * This functions sets the text of the text queue of a LockWindow.
 *
 * @param window Window to set the text in
 * @param text Text to overwrite the text queue buffer with
 */
static void lock_window_text_queue_set_text(LockWindow *window,
                                            const char *text)
{
    gtk_text_buffer_set_text(window->text_queue, text, -1);
}

/**** File ****/

/**
 * This function opens the input file of a LockWindow.
 *
 * @param object https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 * @param result https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 * @param user_data https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 */
static void lock_window_file_open(GObject *source_object, GAsyncResult *res,
                                  gpointer data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    LockWindow *window = LOCK_WINDOW(data);

    window->file_input = gtk_file_dialog_open_finish(dialog, res, NULL);
    if (window->file_input == NULL) {
        /* Cleanup */
        g_object_unref(dialog);
        dialog = NULL;

        window = NULL;

        return;
    }

    adw_action_row_set_subtitle(window->file_input_row,
                                g_file_get_basename(window->file_input));

    /* Cleanup */
    g_object_unref(dialog);
    dialog = NULL;

    window = NULL;
}

/**
 * This function opens the output file of a LockWindow.
 *
 * @param object https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 * @param result https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 * @param user_data https://docs.gtk.org/gio/callback.AsyncReadyCallback.html
 */
static void lock_window_file_save(GObject *source_object,
                                  GAsyncResult *res, gpointer data)
{
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);
    LockWindow *window = LOCK_WINDOW(data);

    window->file_output = gtk_file_dialog_save_finish(dialog, res, NULL);
    if (window->file_output == NULL) {
        /* Cleanup */
        g_object_unref(dialog);
        dialog = NULL;

        window = NULL;

        return;
    }

    adw_action_row_set_subtitle(window->file_output_row,
                                g_file_get_basename(window->file_output));

    /* Cleanup */
    g_object_unref(dialog);
    dialog = NULL;

    window = NULL;
}

/**
 * This function opens an open file dialog for a LockWindow.
 *
 * @param self https://docs.gtk.org/gtk4/signal.Button.clicked.html
 * @param window https://docs.gtk.org/gtk4/signal.Button.clicked.html
 */
static void
lock_window_file_open_dialog_present(GtkButton *self, LockWindow *window)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    GCancellable *cancel = g_cancellable_new();

    gtk_file_dialog_open(dialog, GTK_WINDOW(window),
                         cancel, lock_window_file_open, window);
}

/**
 * This function opens a save file dialog for a LockWindow.
 *
 * @param self https://docs.gtk.org/gtk4/signal.Button.clicked.html
 * @param window https://docs.gtk.org/gtk4/signal.Button.clicked.html
 */
static void
lock_window_file_save_dialog_present(GtkButton *self, LockWindow *window)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    GCancellable *cancel = g_cancellable_new();

    gtk_file_dialog_save(dialog, GTK_WINDOW(window),
                         cancel, lock_window_file_save, window);
}

/**** Encryption ****/

/**
 * This function overwrites the email of a LockWindow.
 *
 * @param window Window to overwrite the email of
 * @param email Email to overwrite with
 */
void lock_window_set_email(LockWindow *window, const char *email)
{
    strcpy(window->email, email);
}

/**
 * This function handles user input to select the target key for a text encryption process of a LockWindow.
 *
 * @param self https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 * @param parameter https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 * @param window https://docs.gtk.org/gio/signal.SimpleAction.activate.html
 */
void lock_window_encrypt_text_dialog(GSimpleAction *self, GVariant *parameter,
                                     LockWindow *window)
{
    LockEntryDialog *dialog =
        lock_entry_dialog_new(_("Encrypt for"), _("Enter email …"),
                              GTK_INPUT_PURPOSE_EMAIL);

    g_signal_connect(dialog, "entered", G_CALLBACK(thread_encrypt_text),
                     window);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
}

/**
 * This function handles user input to select the target key for a file encryption process of a LockWindow.
 *
 * @param self https://docs.gtk.org/gtk4/signal.Button.clicked.html
 * @param window https://docs.gtk.org/gtk4/signal.Button.clicked.html
 */
void lock_window_encrypt_file_dialog(GtkButton *self, LockWindow *window)
{
    LockEntryDialog *dialog =
        lock_entry_dialog_new(_("Encrypt for"), _("Enter email …"),
                              GTK_INPUT_PURPOSE_EMAIL);

    g_signal_connect(dialog, "entered", G_CALLBACK(thread_encrypt_file),
                     window);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window));
}

/**
 * This function encrypts text from the text view of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 */
void lock_window_encrypt_text(LockWindow *window)
{
    gchar *plain = lock_window_text_view_get_text(window);

    gpgme_key_t key = key_from_email(window->email);
    HANDLE_ERROR_EMAIL(, key, lock_window_encrypt_text_on_completed, window,
                       g_free(plain);
                       plain = NULL;
        );
    strcpy(window->email, "");  // Mark email search as successful

    gchar *armor = encrypt_text(plain, key);
    if (armor == NULL) {
        lock_window_text_queue_set_text(window, "");
    } else {
        lock_window_text_queue_set_text(window, armor);
    }

    /* Cleanup */
    g_free(plain);
    plain = NULL;

    g_free(armor);
    armor = NULL;

    gpgme_key_release(key);

    /* UI */
    g_idle_add((GSourceFunc) lock_window_encrypt_text_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for text encryption and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_encrypt_text_on_completed(LockWindow *window)
{
    AdwToast *toast;

    gchar *armor = lock_window_text_queue_get_text(window);
    lock_window_text_queue_set_text(window, "");

    if (strlen(window->email) > 0) {
        toast =
            adw_toast_new(g_strdup_printf
                          (_("Failed to find key for email “%s”"),
                           window->email));

        strcpy(window->email, "");
    } else if (strcmp(armor, "") == 0) {
        toast = adw_toast_new(_("Encryption failed"));
    } else {
        toast = adw_toast_new(_("Text encrypted"));

        lock_window_text_view_set_text(window, armor);
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Cleanup */
    g_free(armor);
    armor = NULL;

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**
 * This function encrypts the file of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 */
void lock_window_encrypt_file(LockWindow *window)
{
    char *input_path = g_file_get_path(window->file_input);
    char *output_path = g_file_get_path(window->file_output);

    gpgme_key_t key = key_from_email(window->email);
    HANDLE_ERROR_EMAIL(, key, lock_window_encrypt_file_on_completed, window,
                       /* Cleanup */
                       g_free(input_path);
                       input_path = NULL; g_free(output_path);
                       output_path = NULL;
        );
    strcpy(window->email, "");  // Mark email search as successful

    bool success = encrypt_file(input_path, output_path, key);
    if (!success) {
        window->file_success = false;
    } else {
        window->file_success = true;
    }

    /* Cleanup */
    g_free(input_path);
    input_path = NULL;

    g_free(output_path);
    output_path = NULL;

    gpgme_key_release(key);

    /* UI */
    g_idle_add((GSourceFunc) lock_window_encrypt_file_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for file encryption and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_encrypt_file_on_completed(LockWindow *window)
{
    AdwToast *toast;

    if (strlen(window->email) > 0) {
        toast =
            adw_toast_new(g_strdup_printf
                          (_("Failed to find key for email “%s”"),
                           window->email));

        strcpy(window->email, "");
    } else if (!window->file_success) {
        toast = adw_toast_new(_("Encryption failed"));
    } else {
        toast = adw_toast_new(_("File encrypted"));
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**** Decryption ****/

/**
 * This function decrypts text from the text view of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_decrypt_text(LockWindow *window)
{
    gchar *armor = lock_window_text_view_get_text(window);

    gchar *plain = decrypt_text(armor);
    if (plain == NULL) {
        lock_window_text_queue_set_text(window, "");
    } else {
        lock_window_text_queue_set_text(window, plain);
    }

    /* Cleanup */
    g_free(armor);
    armor = NULL;

    g_free(plain);
    plain = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_decrypt_text_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for text decryption and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_decrypt_text_on_completed(LockWindow *window)
{
    AdwToast *toast;

    gchar *plain = lock_window_text_queue_get_text(window);
    lock_window_text_queue_set_text(window, "");

    if (strcmp(plain, "") == 0) {
        toast = adw_toast_new(_("Decryption failed"));
    } else {
        toast = adw_toast_new(_("Text decrypted"));

        lock_window_text_view_set_text(window, plain);
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Cleanup */
    g_free(plain);
    plain = NULL;

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**
 * This function decrypts the input file of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_decrypt_file(LockWindow *window)
{
    // FIXME: cannot write to file until GPGME version 1.24

    char *input_path = g_file_get_path(window->file_input);
    char *output_path = g_file_get_path(window->file_output);

    bool success = decrypt_file(input_path, output_path);
    if (!success) {
        window->file_success = false;
    } else {
        window->file_success = true;
    }

    /* Cleanup */
    g_free(input_path);
    input_path = NULL;

    g_free(output_path);
    output_path = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_decrypt_file_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for file decryption and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_decrypt_file_on_completed(LockWindow *window)
{
    AdwToast *toast;

    if (!window->file_success) {
        toast = adw_toast_new(_("Decryption failed"));
    } else {
        toast = adw_toast_new(_("File decrypted"));
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**** Signing ****/

/**
 * This function signs text from the text view of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_sign_text(LockWindow *window)
{
    gchar *plain = lock_window_text_view_get_text(window);

    strcpy(window->email, "");  // Mark email search as successful

    gchar *armor = sign_text(plain);
    if (armor == NULL) {
        lock_window_text_queue_set_text(window, "");
    } else {
        lock_window_text_queue_set_text(window, armor);
    }

    /* Cleanup */
    g_free(plain);
    plain = NULL;

    g_free(armor);
    armor = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_sign_text_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for text signing and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_sign_text_on_completed(LockWindow *window)
{
    AdwToast *toast;

    gchar *armor = lock_window_text_queue_get_text(window);
    lock_window_text_queue_set_text(window, "");

    if (strcmp(armor, "") == 0) {
        toast = adw_toast_new(_("Signing failed"));
    } else {
        toast = adw_toast_new(_("Text signed"));

        lock_window_text_view_set_text(window, armor);
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Cleanup */
    g_free(armor);
    armor = NULL;

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**
 * This function signs the input file of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_sign_file(LockWindow *window)
{
    char *input_path = g_file_get_path(window->file_input);
    char *output_path = g_file_get_path(window->file_output);

    bool success = sign_file(input_path, output_path);
    if (!success) {
        window->file_success = false;
    } else {
        window->file_success = true;
    }

    /* Cleanup */
    g_free(input_path);
    input_path = NULL;

    g_free(output_path);
    output_path = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_sign_file_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for file signing and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_sign_file_on_completed(LockWindow *window)
{
    AdwToast *toast;

    if (!window->file_success) {
        toast = adw_toast_new(_("Signing failed"));
    } else {
        toast = adw_toast_new(_("File signed"));
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**** Verification ****/

/**
 * This function verifies text from the text view of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_verify_text(LockWindow *window)
{
    gchar *armor = lock_window_text_view_get_text(window);

    gchar *plain = verify_text(armor);
    if (plain == NULL) {
        lock_window_text_queue_set_text(window, "");
    } else {
        lock_window_text_queue_set_text(window, plain);
    }

    /* Cleanup */
    g_free(armor);
    armor = NULL;

    g_free(plain);
    plain = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_verify_text_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for text verification and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_verify_text_on_completed(LockWindow *window)
{
    AdwToast *toast;

    gchar *plain = lock_window_text_queue_get_text(window);
    lock_window_text_queue_set_text(window, "");

    if (strcmp(plain, "") == 0) {
        toast = adw_toast_new(_("Verification failed"));
    } else {
        toast = adw_toast_new(_("Text verified"));

        lock_window_text_view_set_text(window, plain);
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Cleanup */
    g_free(plain);
    plain = NULL;

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}

/**
 * This function verifies the input file of a LockWindow.
 *
 * @param window https://docs.gtk.org/glib/callback.ThreadFunc.html
 */
void lock_window_verify_file(LockWindow *window)
{
    // FIXME: cannot write to file until GPGME version 1.24

    char *input_path = g_file_get_path(window->file_input);
    char *output_path = g_file_get_path(window->file_output);

    bool success = verify_file(input_path, output_path);
    if (!success) {
        window->file_success = false;
    } else {
        window->file_success = true;
    }

    /* Cleanup */
    g_free(input_path);
    input_path = NULL;

    g_free(output_path);
    output_path = NULL;

    /* UI */
    g_idle_add((GSourceFunc) lock_window_verify_file_on_completed, window);

    g_thread_exit(0);
}

/**
 * This function handles UI updates for file verification and is supposed to be called via g_idle_add().
 *
 * @param window https://docs.gtk.org/glib/callback.SourceFunc.html
 *
 * @return https://docs.gtk.org/glib/func.idle_add.html
 */
gboolean lock_window_verify_file_on_completed(LockWindow *window)
{
    AdwToast *toast;

    if (!window->file_success) {
        toast = adw_toast_new(_("Verification failed"));
    } else {
        toast = adw_toast_new(_("File verified"));
    }

    adw_toast_set_timeout(toast, 3);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);

    /* Only execute once */
    return false;               // https://docs.gtk.org/glib/func.idle_add.html
}
