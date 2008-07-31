/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#define _GNU_SOURCE  /* for strcasestr() */

#include <termio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#include "mm-serial.h"

G_DEFINE_TYPE (MMSerial, mm_serial, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_DEVICE,
    PROP_BAUD,
    PROP_BITS,
    PROP_PARITY,
    PROP_STOPBITS,
    PROP_SEND_DELAY,

    LAST_PROP
};

#define MM_DEBUG_SERIAL 1
#define SERIAL_BUF_SIZE 2048

#define MM_SERIAL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), MM_TYPE_SERIAL, MMSerialPrivate))

typedef struct {
    int fd;
    GIOChannel *channel;
    struct termios old_t;

    char *device;
    guint baud;
    guint bits;
    char parity;
    guint stopbits;
    guint64 send_delay;

    guint pending_id;
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

#ifdef MM_DEBUG_SERIAL
static inline void
serial_debug (const char *prefix, const char *data, int len)
{
    GString *str;
    int i;

    str = g_string_sized_new (len);
    for (i = 0; i < len; i++) {
        if (data[i] == '\0')
            g_string_append_c (str, ' ');
        else if (data[i] == '\r')
            g_string_append_c (str, '\n');
        else
            g_string_append_c (str, data[i]);
    }

    g_debug ("%s '%s'", prefix, str->str);
    g_string_free (str, TRUE);
}
#else
static inline void
serial_debug (const char *prefix, const char *data, int len)
{
}
#endif /* MM_DEBUG_SERIAL */

/* Timeout handling */

static void
mm_serial_timeout_removed (gpointer data)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (data);

    priv->timeout_id = 0;
}

static gboolean
mm_serial_timed_out (gpointer data)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (data);

    /* Cancel data reading */
    if (priv->pending_id)
        g_source_remove (priv->pending_id);
    else
        g_warning ("Timeout reached, but there's nothing to time out");

    return FALSE;
}

static void
mm_serial_add_timeout (MMSerial *self, guint timeout)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    if (priv->pending_id == 0)
        g_warning ("Adding a time out while not waiting for any data");

    if (priv->timeout_id) {
        g_warning ("Trying to add a new time out while the old one still exists");
        g_source_remove (priv->timeout_id);
    }

    priv->timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT,
                                           timeout * 1000,
                                           mm_serial_timed_out,
                                           self,
                                           mm_serial_timeout_removed);
    if (G_UNLIKELY (priv->timeout_id == 0))
        g_warning ("Registering serial device time out failed.");
}

static void
mm_serial_remove_timeout (MMSerial *self)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    if (priv->timeout_id)
        g_source_remove (priv->timeout_id);
}

/* Pending data reading */

static guint
mm_serial_set_pending (MMSerial *self,
                       guint timeout,
                       GIOFunc callback,
                       gpointer user_data,
                       GDestroyNotify notify)
{
    MMSerialPrivate *priv = MM_SERIAL_GET_PRIVATE (self);

    if (G_UNLIKELY (priv->pending_id)) {
        /* FIXME: Probably should queue up pending calls instead? */
        /* Multiple pending calls on the same GIOChannel doesn't work, so let's cancel the previous one. */
        g_warning ("Adding new pending call while previous one isn't finished.");
        g_warning ("Cancelling the previous pending call.");
        g_source_remove (priv->pending_id);
    }

    priv->pending_id = g_io_add_watch_full (priv->channel,
                                            G_PRIORITY_DEFAULT,
                                            G_IO_IN | G_IO_ERR | G_IO_HUP,
                                            callback, user_data, notify);

    mm_serial_add_timeout (self, timeout);

    return priv->pending_id;
}

static void
mm_serial_pending_done (MMSerial *self)
{
    MM_SERIAL_GET_PRIVATE (self)->pending_id = 0;
    mm_serial_remove_timeout (self);
}

/****/

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

gboolean
mm_serial_open (MMSerial *self)
{
    MMSerialPrivate *priv;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);

    priv = MM_SERIAL_GET_PRIVATE (self);

    g_debug ("(%s) opening serial device...", priv->device);
    priv->fd = open (priv->device, O_RDWR | O_EXCL | O_NONBLOCK | O_NOCTTY);

    if (priv->fd < 0) {
        g_warning ("(%s) cannot open device: %s", priv->device, strerror (errno));
        return FALSE;
    }

    if (ioctl (priv->fd, TCGETA, &priv->old_t) < 0) {
        g_warning ("(%s) cannot control device (errno %d)", priv->device, errno);
        close (priv->fd);
        return FALSE;
    }

    if (!config_fd (self)) {
        close (priv->fd);
        return FALSE;
    }

    priv->channel = g_io_channel_unix_new (priv->fd);

    return TRUE;
}

void
mm_serial_close (MMSerial *self)
{
    MMSerialPrivate *priv;

    g_return_if_fail (MM_IS_SERIAL (self));

    priv = MM_SERIAL_GET_PRIVATE (self);

    if (priv->pending_id)
        g_source_remove (priv->pending_id);

    if (priv->fd) {
        g_message ("Closing device '%s'", priv->device);

        if (priv->channel) {
            g_io_channel_unref (priv->channel);
            priv->channel = NULL;
        }

        ioctl (priv->fd, TCSETA, &priv->old_t);
        close (priv->fd);
        priv->fd = 0;
    }
}

gboolean
mm_serial_send_command (MMSerial *self, GByteArray *command)
{
    MMSerialPrivate *priv;
    int fd;
    int i;
    ssize_t status;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);
    g_return_val_if_fail (command != NULL, FALSE);

    priv = MM_SERIAL_GET_PRIVATE (self);

    fd = priv->fd;

    serial_debug ("Sending:", (char *) command->data, command->len);

    for (i = 0; i < command->len; i++) {
    again:
        status = write (fd, command->data + i, 1);

        if (status < 0) {
            if (errno == EAGAIN)
                goto again;

            g_warning ("Error in writing (errno %d)", errno);
            return FALSE;
        }

        if (priv->send_delay)
            usleep (priv->send_delay);
    }

    return TRUE;
}

gboolean
mm_serial_send_command_string (MMSerial *self, const char *str)
{
    GByteArray *command;
    gboolean ret;

    g_return_val_if_fail (MM_IS_SERIAL (self), FALSE);
    g_return_val_if_fail (str != NULL, FALSE);

    command = g_byte_array_new ();
    g_byte_array_append (command, (guint8 *) str, strlen (str));
    g_byte_array_append (command, (guint8 *) "\r", 1);

    ret = mm_serial_send_command (self, command);
    g_byte_array_free (command, TRUE);

    return ret;
}

typedef struct {
    MMSerial *serial;
    char *terminators;
    GString *result;
    MMSerialGetReplyFn callback;
    gpointer user_data;
} GetReplyInfo;

static void
get_reply_done (gpointer data)
{
    GetReplyInfo *info = (GetReplyInfo *) data;

    mm_serial_pending_done (info->serial);

    /* Call the callback */
    info->callback (info->serial, info->result->str, info->user_data);

    /* Free info */
    g_free (info->terminators);
    g_string_free (info->result, TRUE);

    g_slice_free (GetReplyInfo, info);
}

static gboolean
get_reply_got_data (GIOChannel *source,
                    GIOCondition condition,
                    gpointer data)
{
    GetReplyInfo *info = (GetReplyInfo *) data;
    gsize bytes_read;
    char buf[SERIAL_BUF_SIZE + 1];
    GIOStatus status;
    gboolean done = FALSE;
    int i;

    if (condition & G_IO_HUP || condition & G_IO_ERR) {
        g_string_truncate (info->result, 0);
        return FALSE;
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
            char *p;

            serial_debug ("Got:", buf, bytes_read);

            p = &buf[0];
            for (i = 0; i < bytes_read && !done; i++, p++) {
                int j;
                gboolean is_terminator = FALSE;

                for (j = 0; j < strlen (info->terminators); j++) {
                    if (*p == info->terminators[j]) {
                        is_terminator = TRUE;
                        break;
                    }
                }

                if (is_terminator) {
                    /* Ignore terminators in the beginning of the output */
                    if (info->result->len > 0)
                        done = TRUE;
                } else
                    g_string_append_c (info->result, *p);
            }
        }

        /* Limit the size of the buffer */
        if (info->result->len > SERIAL_BUF_SIZE) {
            g_warning ("%s (%s): response buffer filled before repsonse received",
                       __func__, MM_SERIAL_GET_PRIVATE (info->serial)->device);
            g_string_truncate (info->result, 0);
            done = TRUE;
        }
    } while (!done || bytes_read == SERIAL_BUF_SIZE || status == G_IO_STATUS_AGAIN);

    return !done;
}

guint
mm_serial_get_reply (MMSerial *self,
                     guint timeout,
                     const char *terminators,
                     MMSerialGetReplyFn callback,
                     gpointer user_data)
{
    GetReplyInfo *info;

    g_return_val_if_fail (MM_IS_SERIAL (self), 0);
    g_return_val_if_fail (terminators != NULL, 0);
    g_return_val_if_fail (callback != NULL, 0);

    info = g_slice_new0 (GetReplyInfo);
    info->serial = self;
    info->terminators = g_strdup (terminators);
    info->result = g_string_new (NULL);
    info->callback = callback;
    info->user_data = user_data;

    return mm_serial_set_pending (self, timeout, get_reply_got_data, info, get_reply_done);
}

typedef struct {
    MMSerial *serial;
    char **str_needles;
    char **terminators;
    GString *result;
    MMSerialWaitForReplyFn callback;
    gpointer user_data;
    int reply_index;
    guint timeout;
    time_t start;
} WaitForReplyInfo;

static void
wait_for_reply_done (gpointer data)
{
    WaitForReplyInfo *info = (WaitForReplyInfo *) data;

    mm_serial_pending_done (info->serial);

    /* Call the callback */
    info->callback (info->serial, info->reply_index, info->user_data);

    /* Free info */
    if (info->result)
        g_string_free (info->result, TRUE);

    g_strfreev (info->str_needles);
    g_strfreev (info->terminators);
    g_slice_free (WaitForReplyInfo, info);
}

static gboolean
find_terminator (const char *line, char **terminators)
{
    int i;

    for (i = 0; terminators[i]; i++) {
        if (!strncasecmp (line, terminators[i], strlen (terminators[i])))
            return TRUE;
    }
    return FALSE;
}

static gboolean
find_response (const char *line, char **responses, gint *idx)
{
    int i;

    /* Don't look for a result again if we got one previously */
    for (i = 0; responses[i]; i++) {
        if (strcasestr (line, responses[i])) {
            *idx = i;
            return TRUE;
        }
    }
    return FALSE;
}

#define RESPONSE_LINE_MAX 128

static gboolean
wait_for_reply_got_data (GIOChannel *source,
                         GIOCondition condition,
                         gpointer data)
{
    WaitForReplyInfo *info = (WaitForReplyInfo *) data;
    gchar buf[SERIAL_BUF_SIZE + 1];
    gsize bytes_read;
    GIOStatus status;
    gboolean got_response = FALSE;
    gboolean done = FALSE;

    if (condition & G_IO_HUP || condition & G_IO_ERR)
        return FALSE;

    do {
        GError *err = NULL;

        status = g_io_channel_read_chars (source, buf, SERIAL_BUF_SIZE, &bytes_read, &err);
        if (status == G_IO_STATUS_ERROR) {
            g_warning ("%s", err->message);
            g_error_free (err);
            err = NULL;
        }

        if (bytes_read > 0) {
            buf[bytes_read] = 0;
            g_string_append (info->result, buf);

            serial_debug ("Got:", info->result->str, info->result->len);
        }

        /* Look for needles and terminators */
        if ((bytes_read > 0) && info->result->str) {
            char *p = info->result->str;

            /* Break the response up into lines and process each one */
            while (   (p < info->result->str + strlen (info->result->str))
                      && !(done && got_response)) {
                char line[RESPONSE_LINE_MAX] = { '\0', };
                char *tmp;
                int i;
                gboolean got_something = FALSE;

                for (i = 0; *p && (i < RESPONSE_LINE_MAX - 1); p++) {
                    /* Ignore front CR/LF */
                    if ((*p == '\n') || (*p == '\r')) {
                        if (got_something)
                            break;
                    } else {
                        line[i++] = *p;
                        got_something = TRUE;
                    }
                }
                line[i] = '\0';

                tmp = g_strstrip (line);
                if (tmp && strlen (tmp)) {
                    done = find_terminator (tmp, info->terminators);
                    if (info->reply_index == -1)
                        got_response = find_response (tmp, info->str_needles, &(info->reply_index));
                }
            }

            if (done && got_response)
                break;
        }

        /* Limit the size of the buffer */
        if (info->result->len > SERIAL_BUF_SIZE) {
            g_warning ("%s (%s): response buffer filled before repsonse received",
                       __func__, MM_SERIAL_GET_PRIVATE (info->serial)->device);
            done = TRUE;
            break;
        }

        /* Make sure we don't go over the timeout, in addition to the timeout
         * handler that's been scheduled.  If for some reason this loop doesn't
         * terminate (terminator not found, whatever) then this should make
         * sure that we don't spin the CPU forever.
         */
        if (time (NULL) - info->start > info->timeout + 1) {
            done = TRUE;
            break;
        } else
            g_usleep (50);
    } while (!done || bytes_read == SERIAL_BUF_SIZE || status == G_IO_STATUS_AGAIN);

    return !done;
}

guint
mm_serial_wait_for_reply (MMSerial *self,
                          guint timeout,
                          char **responses,
                          char **terminators,
                          MMSerialWaitForReplyFn callback,
                          gpointer user_data)
{
    WaitForReplyInfo *info;

    g_return_val_if_fail (MM_IS_SERIAL (self), 0);
    g_return_val_if_fail (responses != NULL, 0);
    g_return_val_if_fail (callback != NULL, 0);

    info = g_slice_new0 (WaitForReplyInfo);
    info->serial = self;
    info->str_needles = g_strdupv (responses);
    info->terminators = g_strdupv (terminators);
    info->result = g_string_new (NULL);
    info->callback = callback;
    info->user_data = user_data;
    info->reply_index = -1;
    info->timeout = timeout * 1000;
    info->start = time (NULL);

    return mm_serial_set_pending (self, timeout, wait_for_reply_got_data, info, wait_for_reply_done);
}

#if 0
typedef struct {
    MMSerial *serial;
    gboolean timed_out;
    MMSerialWaitQuietFn callback;
    gpointer user_data;
} WaitQuietInfo;

static void
wait_quiet_done (gpointer data)
{
    WaitQuietInfo *info = (WaitQuietInfo *) data;

    mm_serial_pending_done (info->serial);

    /* Call the callback */
    info->callback (info->serial, info->timed_out, info->user_data);

    /* Free info */
    g_slice_free (WaitQuietInfo, info);
}

static gboolean
wait_quiet_quiettime (gpointer data)
{
    WaitQuietInfo *info = (WaitQuietInfo *) data;

    info->timed_out = FALSE;
    g_source_remove (MM_SERIAL_GET_PRIVATE (info->serial)->pending);

    return FALSE;
}

static gboolean
wait_quiet_got_data (GIOChannel *source,
                     GIOCondition condition,
                     gpointer data)
{
    WaitQuietInfo *info = (WaitQuietInfo *) data;
    gsize bytes_read;
    char buf[4096];
    GIOStatus status;

    if (condition & G_IO_HUP || condition & G_IO_ERR)
        return FALSE;

    if (condition & G_IO_IN) {
        do {
            status = g_io_channel_read_chars (source, buf, 4096, &bytes_read, NULL);

            if (bytes_read) {
                /* Reset the quiet time timeout */
                g_source_remove (info->quiet_id);
                info->quiet_id = g_timeout_add (info->quiet_time, wait_quiet_quiettime, info);
            }
        } while (bytes_read == 4096 || status == G_IO_STATUS_AGAIN);
    }

    return TRUE;
}

void
mm_serial_wait_quiet (MMSerial *self,
                      guint timeout, 
                      guint quiet_time,
                      MMSerialWaitQuietFn callback,
                      gpointer user_data)
{
    WaitQuietInfo *info;

    g_return_if_fail (MM_IS_SERIAL (self));
    g_return_if_fail (callback != NULL);

    info = g_slice_new0 (WaitQuietInfo);
    info->serial = self;
    info->timed_out = TRUE;
    info->callback = callback;
    info->user_data = user_data;
    info->quiet_id = g_timeout_add (quiet_time,
                                    wait_quiet_timeout,
                                    info);

    return mm_serial_set_pending (self, timeout, wait_quiet_got_data, info, wait_quiet_done);
}

#endif

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

    MM_SERIAL_GET_PRIVATE (info->serial)->pending_id = 0;

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

    MM_SERIAL_GET_PRIVATE (self)->pending_id = id;

    return id;
}

GIOChannel *
mm_serial_get_io_channel (MMSerial *self)
{
    MMSerialPrivate *priv;

    g_return_val_if_fail (MM_IS_SERIAL (self), NULL);

    priv = MM_SERIAL_GET_PRIVATE (self);
    if (priv->channel)
        return g_io_channel_ref (priv->channel);

    return NULL;
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
    priv->send_delay = 0;
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
    g_free (priv->device);

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
}
