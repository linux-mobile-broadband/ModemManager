/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_SERIAL_H
#define MM_SERIAL_H

#include <glib/gtypes.h>
#include <glib-object.h>

#define MM_TYPE_SERIAL          (mm_serial_get_type ())
#define MM_SERIAL(obj)          (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SERIAL, MMSerial))
#define MM_SERIAL_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SERIAL, MMSerialClass))
#define MM_IS_SERIAL(obj)       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SERIAL))
#define MM_IS_SERIAL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SERIAL))
#define MM_SERIAL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SERIAL, MMSerialClass))

#define MM_SERIAL_DEVICE     "device"
#define MM_SERIAL_BAUD       "baud"
#define MM_SERIAL_BITS       "bits"
#define MM_SERIAL_PARITY     "parity"
#define MM_SERIAL_STOPBITS   "stopbits"
#define MM_SERIAL_SEND_DELAY "send-delay"

typedef struct {
    GObject parent;
} MMSerial;

typedef struct {
    GObjectClass parent;
} MMSerialClass;

GType mm_serial_get_type (void);

typedef void (*MMSerialGetReplyFn)     (MMSerial *serial,
                                        const char *reply,
                                        gpointer user_data);

typedef void (*MMSerialWaitForReplyFn) (MMSerial *serial,
                                        int reply_index,
                                        gpointer user_data);

typedef void (*MMSerialWaitQuietFn)    (MMSerial *serial,
                                        gboolean timed_out,
                                        gpointer user_data);

typedef void (*MMSerialFlashFn)        (MMSerial *serial,
                                        gpointer user_data);

const char *mm_serial_get_device       (MMSerial *serial);

gboolean mm_serial_open                (MMSerial *self);

void     mm_serial_close               (MMSerial *self);
gboolean mm_serial_send_command        (MMSerial *self,
                                        GByteArray *command);

gboolean mm_serial_send_command_string (MMSerial *self,
                                        const char *str);

guint    mm_serial_get_reply           (MMSerial *self,
                                        guint timeout,
                                        const char *terminators,
                                        MMSerialGetReplyFn callback,
                                        gpointer user_data);

guint    mm_serial_wait_for_reply      (MMSerial *self,
                                        guint timeout,
                                        char **responses,
                                        char **terminators,
                                        MMSerialWaitForReplyFn callback,
                                        gpointer user_data);

void     mm_serial_wait_quiet          (MMSerial *self,
                                        guint timeout, 
                                        guint quiet_time,
                                        MMSerialWaitQuietFn callback,
                                        gpointer user_data);

guint    mm_serial_flash               (MMSerial *self,
                                        guint32 flash_time,
                                        MMSerialFlashFn callback,
                                        gpointer user_data);

GIOChannel *mm_serial_get_io_channel   (MMSerial *self);

#endif /* MM_SERIAL_H */
