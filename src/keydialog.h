#ifndef KEY_DIALOG_H
#define KEY_DIALOG_H

#include <adwaita.h>

#define LOCK_TYPE_KEY_DIALOG (lock_key_dialog_get_type())

G_DECLARE_FINAL_TYPE(LockKeyDialog, lock_key_dialog, LOCK, KEY_DIALOG,
                     AdwDialog);

LockKeyDialog *lock_key_dialog_new();

#endif                          // KEY_DIALOG_H
