
/*
 * C
 *
 * Copyright 2019 Sony Corp . All rights reserved.
 * This file has been modified by MicroEJ Corp.
 */

/****************************************************************************
 * gnss/gnss_main.c
 *
 *
 *
 *   Copyright 2018 Sony Semiconductor Solutions Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Sony Semiconductor Solutions Corporation nor
 *    the names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sdk/config.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include "sni.h"

#include "microej_list.h"
#include "gnss_functions.h"
/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GNSS_POLL_FD_NUM 1
#define GNSS_POLL_TIMEOUT_FOREVER -1
#define MY_GNSS_SIG 18

#define GNSS_DEBUG_TRACE

#ifdef GNSS_DEBUG_TRACE
#define GNSS_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define GNSS_DEBUG_PRINTF(...) ((void)0)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

gnss_info gnss;
MICROEJ_LIST_declare(java_thread_id_list, 10)

    /****************************************************************************
 * Interface Functions
 ****************************************************************************/

    int gnss_stop(void)
{
    /* Stop GNSS. */
    int ret = ioctl(gnss.fd, CXD56_GNSS_IOCTL_STOP, 0);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("stop GNSS GNSS_ERROR\n");
    }
    else
    {
        GNSS_DEBUG_PRINTF("stop GNSS GNSS_OK\n");
    }
    return ret;
}

int gnss_close(void)
{
    struct cxd56_gnss_signal_setting_s setting;
    /* GNSS firmware needs to disable the signal after positioning. */
    setting.enable = 0;
    int ret = 0;
    ioctl(gnss.fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("signal error\n");
    }

    /* Release GNSS file descriptor. */
    ret = close(gnss.fd);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("close error %d\n", errno);
    }

    gnss.running = false;
    sigprocmask(SIG_UNBLOCK, &gnss.mask, NULL);

    pthread_join(gnss.signaling_thread, NULL);

    pthread_mutex_destroy(&gnss.mutex);

    GNSS_DEBUG_PRINTF("End of GNSS Sample:%d\n", ret);

    return ret;
}

/****************************************************************************
 * Name: gnss_read()
 *
 * Description:
 *   Read and print POS data.
 *
 * Input Parameters:
 *
 * Returned Value:
 *   Zero (GNSS_OK) on success; Negative value on error.
 *
 * Assumptions/Limitations:
 *   none.
 *
 ****************************************************************************/
int gnss_read(void)
{
    /* Read POS data. */
    int ret = read(gnss.fd, &gnss.posdat, sizeof(gnss.posdat));
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("read error\n");
        ret = GNSS_ERROR;
        return ret;
    }
    else if (ret != sizeof(gnss.posdat))
    {
        GNSS_DEBUG_PRINTF("read size error\n");
        ret = GNSS_ERROR;
        return ret;
    }
    else
    {
        ret = GNSS_OK;
    }

    /* Print POS data. */
    /* Print time. */
    GNSS_DEBUG_PRINTF(">Hour:%d, minute:%d, sec:%d, usec:%d\n",
                      gnss.posdat.receiver.time.hour, gnss.posdat.receiver.time.minute,
                      gnss.posdat.receiver.time.sec, gnss.posdat.receiver.time.usec);
    if (gnss.posdat.receiver.pos_fixmode != CXD56_GNSS_PVT_POSFIX_INVALID)
    {
        /* 2D fix or 3D fix.
       * Convert latitude and longitude into dmf format and print it. */
        ret = GNSS_OK;
        return ret;
    }
    else
    {
        GNSS_DEBUG_PRINTF(">No Positioning Data\n");
        ret = GNSS_INVALID_DATA;
        return ret;
    }
}

void gnss_signo()
{
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 100 * 1000 * 1000;
    do
    {
        /* Wait for positioning to be fixed. After fixed,
    * idle for the specified seconds. */
        // int ret = sigwaitinfo(&gnss.mask, NULL);
        int ret = sigtimedwait(&gnss.mask, NULL, &timeout);
        if (ret < 0 && errno == EAGAIN)
        {
            // GNSS_DEBUG_PRINTF("sigtimedwait timeout \n");
            if (!gnss.running)
            {
                break;
            }
        }
        else if (ret != MY_GNSS_SIG)
        {
            GNSS_DEBUG_PRINTF("sigwaitinfo error %d\n", ret);
        }
        else
        {
            gnss.data_ready = true;
            microej_list_consume(&java_thread_id_list, &microej_list_thread_id_consume, &microej_list_thread_id_delete);
        }
    } while (gnss.running);
}

int gnss_fetch()
{
    int ret = ERROR;
    int thread_id = SNI_getCurrentJavaThreadID();
    microej_list_add(&java_thread_id_list, thread_id);
    SNI_suspendCurrentJavaThread(thread_id);
    if (gnss.data_ready)
    {
        pthread_mutex_lock(&gnss.mutex);
        ret = gnss_read();
        pthread_mutex_unlock(&gnss.mutex);
    }
    return ret;
}

/****************************************************************************
 * Name: gnss_setparams()
 *
 * Description:
 *   Set gnss parameters use ioctl.
 *
 * Input Parameters:
 *
 * Returned Value:
 *   Zero (GNSS_OK) on success; Negative value on error.
 *
 * Assumptions/Limitations:
 *   none.
 *
 ****************************************************************************/

int gnss_setparams()
{
    uint32_t set_satellite;
    struct cxd56_gnss_ope_mode_param_s set_opemode;

    /* Set the GNSS operation interval. */

    set_opemode.mode = 1;     /* Operation mode:Normal(default). */
    set_opemode.cycle = 1000; /* Position notify cycle(msec step). */

    int ret = ioctl(gnss.fd, CXD56_GNSS_IOCTL_SET_OPE_MODE, (uint32_t)&set_opemode);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("ioctl(CXD56_GNSS_IOCTL_SET_OPE_MODE) NG!!\n");
        return ret;
    }

    /* Set the type of satellite system used by GNSS. */

    set_satellite = CXD56_GNSS_SAT_GPS | CXD56_GNSS_SAT_GLONASS;

    ret = ioctl(gnss.fd, CXD56_GNSS_IOCTL_SELECT_SATELLITE_SYSTEM, set_satellite);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("ioctl(CXD56_GNSS_IOCTL_SELECT_SATELLITE_SYSTEM) NG!!\n");
        ret = GNSS_ERROR;
    }
    return ret;
}

int gnss_init(void)
{

    microej_list_initialize(&java_thread_id_list);
    pthread_mutex_init(&gnss.mutex, NULL);

    struct cxd56_gnss_signal_setting_s setting;

    /* Program start. */
    GNSS_DEBUG_PRINTF("Hello, GNSS(USE_SIGNAL) SAMPLE!!\n");

    /* Get file descriptor to control GNSS. */
    gnss.fd = open("/dev/gps", O_RDONLY);
    if (gnss.fd < 0)
    {
        GNSS_DEBUG_PRINTF("open error:%d,%d\n", gnss.fd, errno);
        return -ENODEV;
    }

    /* Configure mask to notify GNSS signal. */
    sigemptyset(&gnss.mask);
    sigaddset(&gnss.mask, MY_GNSS_SIG);
    int ret = sigprocmask(SIG_BLOCK, &gnss.mask, NULL);
    if (ret != GNSS_OK)
    {
        GNSS_DEBUG_PRINTF("sigprocmask failed. %d\n", ret);
        gnss_close();
        return GNSS_ERROR;
    }

    /* Set the signal to notify GNSS events. */
    setting.fd = gnss.fd;
    setting.enable = 1;
    setting.gnsssig = CXD56_GNSS_SIG_GNSS;
    setting.signo = MY_GNSS_SIG;
    setting.data = NULL;

    ret = ioctl(gnss.fd, CXD56_GNSS_IOCTL_SIGNAL_SET, (unsigned long)&setting);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("signal error\n");
        gnss_close();
        return GNSS_ERROR;
    }

    /* Set GNSS parameters. */
    ret = gnss_setparams();
    if (ret != GNSS_OK)
    {
        GNSS_DEBUG_PRINTF("gnss_setparams failed. %d\n", ret);
        gnss_close();
        return GNSS_ERROR;
    }

    gnss.running = true;
    /* example for starting another thread in native C code */
    pthread_attr_t attr;
    int result = pthread_attr_init(&attr);
    // Initialize pthread such as its resource will be
    assert(result == 0);
    result = pthread_attr_setstacksize(&attr, 512);
    assert(result == 0);
    result = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    assert(result == 0);
    result = pthread_create(&gnss.signaling_thread, &attr, &gnss_signo, NULL);
    pthread_attr_destroy(&attr);

    return ret;
}

bool gnss_start(void)
{
    /* Start GNSS. */
    int ret = ioctl(gnss.fd, CXD56_GNSS_IOCTL_START, CXD56_GNSS_STMOD_HOT);
    if (ret < 0)
    {
        GNSS_DEBUG_PRINTF("start GNSS GNSS_ERROR %d\n", errno);
        gnss_stop();
        ret = GNSS_ERROR;
    }
    else
    {
        GNSS_DEBUG_PRINTF("start GNSS GNSS_OK\n");
        ret = GNSS_OK;
    }
    return ret;
}

float gnss_get_latitude()
{
    pthread_mutex_lock(&gnss.mutex);
    double lat = gnss.posdat.receiver.latitude;
    pthread_mutex_unlock(&gnss.mutex);
    return lat;
}
float gnss_get_longitude()
{
    pthread_mutex_lock(&gnss.mutex);
    double longi = gnss.posdat.receiver.longitude;
    pthread_mutex_unlock(&gnss.mutex);
    return longi;
}
float gnss_get_altitude()
{
    pthread_mutex_lock(&gnss.mutex);
    double alti = gnss.posdat.receiver.altitude;
    pthread_mutex_unlock(&gnss.mutex);
    return alti;
}
float gnss_get_velocity()
{
    pthread_mutex_lock(&gnss.mutex);
    double velo = gnss.posdat.receiver.velocity;
    pthread_mutex_unlock(&gnss.mutex);
    return velo;
}


int64_t gnss_get_time()
{
    pthread_mutex_lock(&gnss.mutex);
    int year = gnss.posdat.receiver.date.year;
    int month = gnss.posdat.receiver.date.month;
    int day = gnss.posdat.receiver.date.day;
    int hour = gnss.posdat.receiver.time.hour;
    int minute = gnss.posdat.receiver.time.minute;
    int sec = gnss.posdat.receiver.time.sec;
    int usec = gnss.posdat.receiver.time.usec;
    pthread_mutex_unlock(&gnss.mutex);
    struct tm t = {0};
    t.tm_year   = year;
    t.tm_mon    = month;
    t.tm_mday   = day;
    t.tm_hour   = hour;
    t.tm_min    = minute;
    t.tm_sec    = sec;
    time_t local = mktime(&t);
    int64_t epoch_in_ms = local*1000 + usec/1000;
    return epoch_in_ms;
}
