/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef MM_MODEM_HUAWEI_H
#define MM_MODEM_HUAWEI_H

#include "mm-generic-gsm.h"

#define MM_TYPE_MODEM_HUAWEI			(mm_modem_huawei_get_type ())
#define MM_MODEM_HUAWEI(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), MM_TYPE_MODEM_HUAWEI, MMModemHuawei))
#define MM_MODEM_HUAWEI_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),  MM_TYPE_MODEM_HUAWEI, MMModemHuaweiClass))
#define MM_IS_MODEM_HUAWEI(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), MM_TYPE_MODEM_HUAWEI))
#define MM_IS_MODEM_HUAWEI_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass),  MM_TYPE_MODEM_HUAWEI))
#define MM_MODEM_HUAWEI_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj),  MM_TYPE_MODEM_HUAWEI, MMModemHuaweiClass))

#define MM_MODEM_HUAWEI_MONITOR_DEVICE "monitor-device"

typedef struct {
    MMGenericGsm parent;
} MMModemHuawei;

typedef struct {
    MMGenericGsmClass parent;
} MMModemHuaweiClass;

GType mm_modem_huawei_get_type (void);

MMModem *mm_modem_huawei_new (const char *data_device,
                              const char *monitor_device,
                              const char *driver);

#endif /* MM_MODEM_HUAWEI_H */
