/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#define _GNU_SOURCE  /* for strcasestr() */

#include <stdio.h>
#include <stdlib.h>
#include <termio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#include "mm-serial.h"
#include "mm-errors.h"
#include "mm-options.h"

static gboolean mm_serial_queue_process (gpointer data);

G_DEFINE_TYPE (MMSerial, mm_serial, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_BAUD,
    PROP_BITS,
    PROP_PARITY,
    PROP_STOPBITS,
    PROP_SEND_DELAY,
    PROP_CARRIER_DETECT,

    LAST_PROP
};

#define SERIAL_BUF_SIZE 2048

#define MM_SERIAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_SERIAL, MMSerialPrivate))

typedef struct {
    int fd;
    GIOChannel *channel;
    GQueue *queue;
    GString *command;
    GString *response;

    /* Response parser data */
    MMSerialResponseParserFn response_parser_fn;
    gpointer response_parser_user_data;
    GDestroyNotify response_parser_notify;

    struct termios old_t;

    char *device;
    guint baud;
    guint bits;
    char parity;
    guint stopbits;
    guint64 send_delay;
    gboolean carrier_detect;

    guint queue_schedule;
    guint watch_id;
    guint timeout_id;
} MMSerialPrivate;

const char *
mm_serial_get_device (MMSerial *serial)
{
    g_return_val_if_fail (MM_IS_SERIAL (serial), NULL);

    return MM_SERIAL_GET_PRIVATE (serial)->device;
}

static int
parse_baudrate (guint i)
{
    int speed;

    switch (i) {
    case 0:
        speed = B0;
        break;
    case 50:
        speed = B50;
        break;
    case 75:
        speed = B75;
        break;
    case 110:
        speed = B110;
        break;
    case 150:
        speed = B150;
        break;
    case 300:
        speed = B300;
        break;
    case 600:
        speed = B600;
        break;
    case 1200:
        speed = B1200;
        break;
    case 2400:
        speed = B2400;
        break;
    case 4800:
        speed = B4800;
        break;
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    case 460800:
        speed = B460800;
        break;
    default:
        g_warning ("Invalid baudrate '%d'", i);
        speed = B9600;
    }

    return speed;
}

static int
parse_bits (guint i)
{
    int bits;

    switch (i) {
    case 5:
        bits = CS5;
        break;
    case 6:
        bits = CS6;
        break;
    case 7:
        bits = CS7;
        break;
    case 8:
        bits = CS8;
        break;
    default:
        g_warning ("Invalid bits (%d). Valid values are 5, 6, 7, 8.", i);
        bits = CS8;
    }

    return bits;
}

static int
parse_parity (char c)
{
    int parity;

    switch (c) {
    case 'n':
    case 'N':
        parity = 0;
        break;
    case 'e':
    case 'E':
        parity = PARENB;
        break;
    case 'o':
    case 'O':
        parity = PARENB | PARODD;
        break;
    default:
        g_warning ("Invalid parity (%c). Valid values are n, e, o", c);
        parity = 0;
    }

    return parity;
}

static int
parse_stopbits (guint i)
{
    int stopbits;

    switch (i) {
    case 1:
        stopbits = 0;
        break;
    case 2:
        stopbits = CSTOPB;
        break;
    default:
        g_warning ("Invalid stop bits (%d). Valid values are 1 and 2)", i);
        stopbits = 0;
    }

    return stopbits;
}

static gboolean
config_fd (MMSerial *self)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    struct termio stbuf;
    int speed;
    int bits;
    int parity;
    int stopbits;

    speed = parse_baudrate (priv->baud);
    bits = parse_bits (priv->bits);
    parity = parse_parity (priv->parity);
    stopbits = parse_stopbits (priv->stopbits);

    ioctl (priv->fd, TCGETA, &stbuf);

    stbuf.c_iflag &= ~(IGNCR | ICRNL | IUCLC | INPCK | IXON | IXANY | IGNPAR );
    stbuf.c_oflag &= ~(OPOST | OLCUC | OCRNL | ONLCR | ONLRET);
    stbuf.c_lflag &= ~(ICANON | XCASE | ECHO | ECHOE | ECHONL);
    stbuf.c_lflag &= ~(ECHO | ECHOE);
    stbuf.c_cc[VMIN] = 1;
    stbuf.c_cc[VTIME] = 0;
    stbuf.c_cc[VEOF] = 1;

    stbuf.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | CLOCAL | PARENB);
    stbuf.c_cflag |= (speed | bits | CREAD | 0 | parity | stopbits);

    if (ioctl (priv->fd, TCSETA, &stbuf) < 0) {
        g_warning ("(%s) cannot control device (errno %d)", priv->device, errno);
        return FALSE;
    }

    return TRUE;
}

static void
serial_debug (const char *prefix, const char *buf, int len)
{
    static GString *debug = NULL;
    const char *s;

    if (!mm_options_debug ())
        return;

    if (len < 0)
        len = strlen (buf);

    if (!debug)
        debug = g_string_sized_new (256);

    g_string_append (debug, prefix);
    g_string_append (debug, " '");
    
    s = buf;
    while (len--) {
        if (g_ascii_isprint (*s))
            g_string_append_c (debug, *s);
        else if (*s == '\r')
            g_string_append (debug, "<CR>");
        else if (*s == '\n')
            g_string_append (debug, "<LF>");
        else
            g_string_append_printf (debug, "\\%d", *s);

        s++;
    }

    g_string_append_c (debug, '\'');
    g_debug (debug->str);
    g_string_truncate (debug, 0);
}

static gboolean
mm_serial_send_command (MMSerial *self,
                        const char *command,
                        GError **error)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    const char *s;
    int status;
    int eagain_count = 1000;

    if (priv->fd == 0) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_SEND_FAILED,
                     "%s", "Sending command failed: device is not enabled");
        return FALSE;
    }

    if (mm_serial_is_connected (self)) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_SEND_FAILED,
                     "%s", "Sending command failed: device is connected");
        return FALSE;
    }

    g_string_truncate (priv->command, g_str_has_prefix (command, "AT") ? 0 : 2);
    g_string_append (priv->command, command);

    if (command[strlen (command)] != '\r')
        g_string_append_c (priv->command, '\r');

    serial_debug ("-->", priv->command->str, -1);

    s = priv->command->str;
    while (*s) {
        status = write (priv->fd, s, 1);
        if (status < 0) {
            if (errno == EAGAIN) {
                eagain_count--;
                if (eagain_count <= 0) {
                    g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_SEND_FAILED,
                                 "Sending command failed: '%s'", strerror (errno));
                    break;
                }
            } else {
                g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_SEND_FAILED,
                             "Sending command failed: '%s'", strerror (errno));
                break;
            }
        } else
            s++;

        if (priv->send_delay)
            usleep (priv->send_delay);
    }

    return *s == '\0';
}

typedef struct {
    char *command;
    MMSerialResponseFn callback;
    gpointer user_data;
    guint32 timeout;
} MMQueueData;

static void
mm_serial_schedule_queue_process (MMSerial *self)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    GSource *source;

    if (priv->queue_schedule)
        /* Already scheduled */
        return;

    source = g_idle_source_new ();
    g_source_set_closure (source, g_cclosure_new_object (G_CALLBACK (mm_serial_queue_process), G_OBJECT (self)));
    g_source_attach (source, NULL);
    priv->queue_schedule = g_source_get_id (source);
    g_source_unref (source);
}

static void
mm_serial_got_response (MMSerial *self, GError *error)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    MMQueueData *info;

    if (priv->timeout_id)
        g_source_remove (priv->timeout_id);

    info = (MMQueueData *) g_queue_pop_head (priv->queue);
    if (info) {
        if (info->callback)
            info->callback (self, priv->response, error, info->user_data);

        g_free (info->command);
        g_slice_free (MMQueueData, info);
    }

    if (error)
        g_error_free (error);

    g_string_truncate (priv->response, 0);
    if (!g_queue_is_empty (priv->queue))
        mm_serial_schedule_queue_process (self);
}

static gboolean
mm_serial_timed_out (gpointer data)
{
    MMSerial *self = MM_SERIAL (data);
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    GError *error;

    priv->timeout_id = 0;

    error = g_error_new_literal (MM_SERIAL_ERROR,
                                 MM_SERIAL_RESPONSE_TIMEOUT,
                                 "Serial command timed out");
    /* FIXME: This is not completely correct - if the response finally arrives and there's
       some other command waiting for response right now, the other command will
       get the output of the timed out command. Not sure what to do here. */
    mm_serial_got_response (self, error);

    return FALSE;
}

static gboolean
mm_serial_queue_process (gpointer data)
{
    MMSerial *self = MM_SERIAL (data);
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    MMQueueData *info;
    GError *error = NULL;

    priv->queue_schedule = 0;

    info = (MMQueueData *) g_queue_peek_head (priv->queue);
    if (!info)
        return FALSE;

    if (mm_serial_send_command (self, info->command, &error)) {
        GSource *source;

        source = g_timeout_source_new (info->timeout);
        g_source_set_closure (source, g_cclosure_new_object (G_CALLBACK (mm_serial_timed_out), G_OBJECT (self)));
        g_source_attach (source, NULL);
        priv->timeout_id = g_source_get_id (source);
        g_source_unref (source);
    } else {
        mm_serial_got_response (self, error);
    }

    return FALSE;
}

void
mm_serial_set_response_parser (MMSerial *self,
                               MMSerialResponseParserFn fn,
                               gpointer user_data,
                               GDestroyNotify notify)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    g_return_if_fail (MM_IS_SERIAL (self));

    if (priv->response_parser_notify)
        priv->response_parser_notify (priv->response_parser_user_data);

    priv->response_parser_fn = fn;
    priv->response_parser_user_data = user_data;
    priv->response_parser_notify = notify;
}

static gboolean
parse_response (MMSerial *self,
                GString *response,
                GError **error)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    g_return_val_if_fail (priv->response_parser_fn != NULL, FALSE);

    return priv->response_parser_fn (priv->response_parser_user_data, response, error);
}

static gboolean
data_available (GIOChannel *source,
                GIOCondition condition,
                gpointer data)
{
    MMSerial *self = MM_SERIAL (data);
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    char buf[SERIAL_BUF_SIZE + 1];
    gsize bytes_read;
    GIOStatus status;

    if (condition & G_IO_HUP) {
        g_string_truncate (priv->response, 0);
        mm_serial_close (self);
        return FALSE;
    }

    if (condition & G_IO_ERR) {
        g_string_truncate (priv->response, 0);
        return TRUE;
    }

    do {
        GError *err = NULL;

        status = g_io_channel_read_chars (source, buf, SERIAL_BUF_SIZE, &bytes_read, &err);
        if (status == G_IO_STATUS_ERROR) {
            g_warning ("%s", err->message);
            g_error_free (err);
            err = NULL;
        }

        if (bytes_read > 0) {
            serial_debug ("<--", buf, bytes_read);
            g_string_append_len (priv->response, buf, bytes_read);
        }

        /* Make sure the string doesn't grow too long */
		if (priv->response->len > SERIAL_BUF_SIZE) {
			g_warning ("%s (%s): response buffer filled before repsonse received",
			           G_STRFUNC, mm_serial_get_device (self));
			g_string_erase (priv->response, 0, (SERIAL_BUF_SIZE / 2));
        }

        if (parse_response (self, priv->response, &err))
            mm_serial_got_response (self, err);
    } while (bytes_read == SERIAL_BUF_SIZE || status == G_IO_STATUS_AGAIN);

    return TRUE;
}

gboolean
mm_serial_open (MMSerial *self, GError **error)
{
    MMSerialPrivate *priv;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);

    priv = MM_SERIAL_GET_PRIVATE (self);

    if (priv->fd)
        /* Already open */
        return TRUE;

    g_debug ("(%s) opening serial device...", priv->device);
    priv->fd = open (priv->device, O_RDWR | O_EXCL | O_NONBLOCK | O_NOCTTY);

    if (priv->fd < 0) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_OPEN_FAILED,
                     "Could not open serial device %s: %s", priv->device, strerror (errno));
        return FALSE;
    }

    if (ioctl (priv->fd, TCGETA, &priv->old_t) < 0) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_OPEN_FAILED,
                     "Could not open serial device %s: %s", priv->device, strerror (errno));
        close (priv->fd);
        return FALSE;
    }

    if (!config_fd (self)) {
        g_set_error (error, MM_SERIAL_ERROR, MM_SERIAL_OPEN_FAILED,
                     "Could not open serial device %s: %s", priv->device, strerror (errno));
        close (priv->fd);
        priv->fd = 0;
        return FALSE;
    }

    priv->channel = g_io_channel_unix_new (priv->fd);
    priv->watch_id = g_io_add_watch (priv->channel,
                                     G_IO_IN | G_IO_ERR | G_IO_HUP,
                                     data_available, self);

    return TRUE;
}

void
mm_serial_close (MMSerial *self)
{
    MMSerialPrivate *priv;

    g_return_if_fail (MM_IS_SERIAL (self));

    priv = MM_SERIAL_GET_PRIVATE (self);

    if (priv->fd) {
        g_message ("Closing device '%s'", priv->device);

        if (priv->channel) {
            g_source_remove (priv->watch_id);
            g_io_channel_shutdown (priv->channel, TRUE, NULL);
            g_io_channel_unref (priv->channel);
            priv->channel = NULL;
        }

        ioctl (priv->fd, TCSETA, &priv->old_t);
        close (priv->fd);
        priv->fd = 0;
    }
}

void
mm_serial_queue_command (MMSerial *self,
                         const char *command,
                         guint32 timeout_seconds,
                         MMSerialResponseFn callback,
                         gpointer user_data)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);
    MMQueueData *info;

    g_return_if_fail (MM_IS_SERIAL (self));
    g_return_if_fail (command != NULL);

    info = g_slice_new0 (MMQueueData);
    info->command = g_strdup (command);
    info->timeout = timeout_seconds * 1000;
    info->callback = callback;
    info->user_data = user_data;

    g_queue_push_tail (priv->queue, info);

    if (g_queue_get_length (priv->queue) == 1)
        mm_serial_schedule_queue_process (self);
}

typedef struct {
    MMSerial *serial;
    speed_t current_speed;
    MMSerialFlashFn callback;
    gpointer user_data;
} FlashInfo;

static speed_t
get_speed (MMSerial *self)
{
    struct termios options;

    tcgetattr (MM_SERIAL_GET_PRIVATE (self)->fd, &options);

    return cfgetospeed (&options);
}

static void
set_speed (MMSerial *self, speed_t speed)
{
    struct termios options;
    int fd;

    fd = MM_SERIAL_GET_PRIVATE (self)->fd;
    tcgetattr (fd, &options);

    cfsetispeed (&options, speed);
    cfsetospeed (&options, speed);

    options.c_cflag |= (CLOCAL | CREAD);
    tcsetattr (fd, TCSANOW, &options);
}

static void
flash_done (gpointer data)
{
    FlashInfo *info = (FlashInfo *) data;

    info->callback (info->serial, info->user_data);

    g_slice_free (FlashInfo, info);
}

static gboolean
flash_do (gpointer data)
{
    FlashInfo *info = (FlashInfo *) data;

    set_speed (info->serial, info->current_speed);

    return FALSE;
}

guint
mm_serial_flash (MMSerial *self,
                 guint32 flash_time,
                 MMSerialFlashFn callback,
                 gpointer user_data)
{
    FlashInfo *info;
    guint id;

    g_return_val_if_fail (MM_IS_SERIAL (self), 0);
    g_return_val_if_fail (callback != NULL, 0);

    info = g_slice_new0 (FlashInfo);
    info->serial = self;
    info->current_speed = get_speed (self);
    info->callback = callback;
    info->user_data = user_data;

    set_speed (self, B0);

    id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                             flash_time,
                             flash_do,
                             info,
                             flash_done);

    return id;
}

gboolean
mm_serial_is_connected (MMSerial *self)
{
    MMSerialPrivate *priv;
    int mcs = 0;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);

    priv = MM_SERIAL_GET_PRIVATE (self);

    if (!priv->carrier_detect)
        return FALSE;

    if (priv->fd == 0)
        return FALSE;

    if (ioctl (priv->fd, TIOCMGET, &mcs) < 0)
        return FALSE;

    return mcs & TIOCM_CAR ? TRUE : FALSE;
}

/*****************************************************************************/

static void
mm_serial_init (MMSerial *self)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    priv->baud = 57600;
    priv->bits = 8;
    priv->parity = 'n';
    priv->stopbits = 1;
    priv->send_delay = 1000;
    priv->carrier_detect = TRUE;

    priv->queue = g_queue_new ();
    priv->command  = g_string_new_len   ("AT", SERIAL_BUF_SIZE);
    priv->response = g_string_sized_new (SERIAL_BUF_SIZE);
}

static GObject*
constructor (GType type,
             guint n_construct_params,
             GObjectConstructParam *construct_params)
{
    GObject *object;
    MMSerialPrivate *priv;

    object = G_OBJECT_CLASS (mm_serial_parent_class)->constructor (type,
                                                                   n_construct_params,
                                                                   construct_params);
    if (!object)
        return NULL;

    priv = MM_SERIAL_GET_PRIVATE (object);

    if (!priv->device) {
        g_warning ("No device provided");
        g_object_unref (object);
        return NULL;
    }

    return object;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        /* Construct only */
        priv->device = g_value_dup_string (value);
        break;
    case PROP_BAUD:
        priv->baud = g_value_get_uint (value);
        break;
    case PROP_BITS:
        priv->bits = g_value_get_uint (value);
        break;
    case PROP_PARITY:
        priv->parity = g_value_get_char (value);
        break;
    case PROP_STOPBITS:
        priv->stopbits = g_value_get_uint (value);
        break;
    case PROP_SEND_DELAY:
        priv->send_delay = g_value_get_uint64 (value);
        break;
    case PROP_CARRIER_DETECT:
        priv->carrier_detect = g_value_get_boolean (value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (object);

    switch (prop_id) {
    case PROP_DEVICE:
        g_value_set_string (value, priv->device);
        break;
    case PROP_BAUD:
        g_value_set_uint (value, priv->baud);
        break;
    case PROP_BITS:
        g_value_set_uint (value, priv->bits);
        break;
    case PROP_PARITY:
        g_value_set_char (value, priv->parity);
        break;
    case PROP_STOPBITS:
        g_value_set_uint (value, priv->stopbits);
        break;
    case PROP_SEND_DELAY:
        g_value_set_uint64 (value, priv->send_delay);
        break;
    case PROP_CARRIER_DETECT:
        g_value_set_boolean (value, priv->carrier_detect);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
finalize (GObject *object)
{
    MMSerial *self = MM_SERIAL (object);
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    mm_serial_close (self);

    g_queue_free (priv->queue);
    g_string_free (priv->command, TRUE);
    g_string_free (priv->response, TRUE);
    g_free (priv->device);

    if (priv->response_parser_notify)
        priv->response_parser_notify (priv->response_parser_user_data);

    G_OBJECT_CLASS (mm_serial_parent_class)->finalize (object);
}

static void
mm_serial_class_init (MMSerialClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (object_class, sizeof (MMSerialPrivate));

    /* Virtual methods */
    object_class->constructor = constructor;
    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize = finalize;

    /* Properties */
    g_object_class_install_property
        (object_class, PROP_DEVICE,
         g_param_spec_string (MM_SERIAL_DEVICE,
                              "Device",
                              "Serial device",
                              NULL,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class, PROP_BAUD,
         g_param_spec_uint (MM_SERIAL_BAUD,
                            "Baud",
                            "Baud rate",
                            0, G_MAXUINT, 57600,
                            G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_BITS,
         g_param_spec_uint (MM_SERIAL_BITS,
                            "Bits",
                            "Bits",
                            5, 8, 8,
                            G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_PARITY,
         g_param_spec_char (MM_SERIAL_PARITY,
                            "Parity",
                            "Parity",
                            'E', 'o', 'n',
                            G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_STOPBITS,
         g_param_spec_uint (MM_SERIAL_STOPBITS,
                            "Stopbits",
                            "Stopbits",
                            1, 2, 1,
                            G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_SEND_DELAY,
         g_param_spec_uint64 (MM_SERIAL_SEND_DELAY,
                              "SendDelay",
                              "Send delay",
                              0, G_MAXUINT64, 0,
                              G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class, PROP_CARRIER_DETECT,
         g_param_spec_boolean (MM_SERIAL_CARRIER_DETECT,
                               "CarrierDetect",
                               "Has carrier detect",
                               TRUE,
                               G_PARAM_READWRITE));
}
