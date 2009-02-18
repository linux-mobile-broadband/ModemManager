/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_SERIAL_H
#define MM_SERIAL_H

#include <glib.h>
#include <glib/gtypes.h>
#include <glib-object.h>

#define MM_TYPE_SERIAL            (mm_serial_get_type ())
#define MM_SERIAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_SERIAL, MMSerial))
#define MM_SERIAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_SERIAL, MMSerialClass))
#define MM_IS_SERIAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_SERIAL))
#define MM_IS_SERIAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_SERIAL))
#define MM_SERIAL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_SERIAL, MMSerialClass))

#define MM_SERIAL_DEVICE     "serial-device"
#define MM_SERIAL_BAUD       "baud"
#define MM_SERIAL_BITS       "bits"
#define MM_SERIAL_PARITY     "parity"
#define MM_SERIAL_STOPBITS   "stopbits"
#define MM_SERIAL_SEND_DELAY "send-delay"
#define MM_SERIAL_CARRIER_DETECT "carrier-detect"

typedef struct _MMSerial MMSerial;
typedef struct _MMSerialClass MMSerialClass;

typedef gboolean (*MMSerialResponseParserFn) (gpointer user_data,
                                              GString *response,
                                              GError **error);

typedef void (*MMSerialUnsolicitedMsgFn) (MMSerial *serial,
                                          GMatchInfo *match_info,
                                          gpointer user_data);

typedef void (*MMSerialResponseFn)     (MMSerial *serial,
                                        GString *response,
                                        GError *error,
                                        gpointer user_data);

typedef void (*MMSerialFlashFn)        (MMSerial *serial,
                                        gpointer user_data);

struct _MMSerial {
    GObject parent;
};

struct _MMSerialClass {
    GObjectClass parent;
};

GType mm_serial_get_type (void);

void     mm_serial_add_unsolicited_msg_handler (MMSerial *self,
                                                GRegex *regex,
                                                MMSerialUnsolicitedMsgFn callback,
                                                gpointer user_data,
                                                GDestroyNotify notify);

void     mm_serial_set_response_parser (MMSerial *self,
                                        MMSerialResponseParserFn fn,
                                        gpointer user_data,
                                        GDestroyNotify notify);

gboolean mm_serial_open              (MMSerial *self,
                                      GError  **error);

void     mm_serial_close             (MMSerial *self);
void     mm_serial_queue_command     (MMSerial *self,
                                      const char *command,
                                      guint32 timeout_seconds,
                                      MMSerialResponseFn callback,
                                      gpointer user_data);

guint    mm_serial_flash             (MMSerial *self,
                                      guint32 flash_time,
                                      MMSerialFlashFn callback,
                                      gpointer user_data);

gboolean mm_serial_is_connected      (MMSerial *self);
const char *mm_serial_get_device     (MMSerial *self);

#endif /* MM_SERIAL_H */
