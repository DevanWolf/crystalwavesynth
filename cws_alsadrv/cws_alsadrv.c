/**
 *
 *  Copyright (C) 2022-2026 Roman Pauer
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is furnished to do
 *  so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 */

#define _FILE_OFFSET_BITS 64
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <pwd.h>
#include <alsa/asoundlib.h>
#include "cwssynth.h"

#ifdef PANDORA
#define secure_getenv __secure_getenv
#endif


static const char midi_name[] = "CrystalWave Synth";
static const char port_name[] = "CrystalWave Synth port";

static snd_seq_t *midi_seq;
static int midi_port_id;
static pthread_t midi_thread;
static snd_pcm_t *midi_pcm;
static volatile int midi_init_state;
static volatile int midi_event_written;

static int frequency, polyphony, volume, chorus_effect, reverb_effect, enhanced_mode, daemonize;
static int chorus_depth, chorus_rate;
static float reverb_gain[4], reverb_del[4], reverb_fc, reverb_fwd;
static const char *data_filepath = "cwcwtsmp.dat";

static int datafile_len;
static uint8_t *datafile_ptr;
static event_buffer_t event_buffer;
static int16_t output_buffer[48000*2];
static int16_t *output_ptr[100];
static uint16_t output_num_samples[100];
static int output_counter, samples_per_call;

#ifdef __cplusplus
extern "C" {
#endif

event_buffer_t *cws_event_buffer;

#ifdef __cplusplus
}
#endif

static struct timespec start_time;

#if defined(CLOCK_MONOTONIC_RAW)
    static clockid_t monotonic_clock_id;

    #define MONOTONIC_CLOCK_TYPE monotonic_clock_id
#else
    #define MONOTONIC_CLOCK_TYPE CLOCK_MONOTONIC
#endif


static uint32_t CWS_GetTime(void)
{
    struct timespec _tp;

    clock_gettime(MONOTONIC_CLOCK_TYPE, &_tp);

    return ((_tp.tv_sec - start_time.tv_sec) * 1000) + ((_tp.tv_nsec - start_time.tv_nsec) / 1000000);
}


static void set_thread_scheduler(void) __attribute__((noinline));
static void set_thread_scheduler(void)
{
    struct sched_param param;

    memset(&param, 0, sizeof(struct sched_param));
    param.sched_priority = sched_get_priority_min(SCHED_FIFO);
    if (param.sched_priority > 0)
    {
        sched_setscheduler(0, SCHED_FIFO, &param);
    }
}

static void wait_for_midi_initialization(void) __attribute__((noinline));
static void wait_for_midi_initialization(void)
{
    while (midi_init_state == 0)
    {
        struct timespec req;

        req.tv_sec = 0;
        req.tv_nsec = 10000000;
        nanosleep(&req, NULL);
    };
}

static void subscription_event(snd_seq_event_t *event) __attribute__((noinline));
static void subscription_event(snd_seq_event_t *event)
{
    snd_seq_client_info_t *cinfo;
    int err;

    snd_seq_client_info_alloca(&cinfo);
    err = snd_seq_get_any_client_info(midi_seq, event->data.connect.sender.client, cinfo);
    if (err >= 0)
    {
        if (event->type == SND_SEQ_EVENT_PORT_SUBSCRIBED)
        {
            printf("Client subscribed: %s\n", snd_seq_client_info_get_name(cinfo));
        }
        else
        {
            printf("Client unsubscribed: %s\n", snd_seq_client_info_get_name(cinfo));
        }
    }
    else
    {
        printf("Client unsubscribed\n");
    }
}

static void process_event(snd_seq_event_t *event)
{
    uint32_t event_time;

    event_time = CWS_GetTime();

    switch (event->type)
    {
        case SND_SEQ_EVENT_NOTEON:
            CWS_MidiMessageShort(event_time, 0x90 | event->data.note.channel | (event->data.note.note << 8) | (event->data.note.velocity << 16));
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Note ON, channel:%d note:%d velocity:%d\n", event->data.note.channel, event->data.note.note, event->data.note.velocity);
#endif
            break;

        case SND_SEQ_EVENT_NOTEOFF:
            CWS_MidiMessageShort(event_time, 0x80 | event->data.note.channel | (event->data.note.note << 8) | (event->data.note.velocity << 16));
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Note OFF, channel:%d note:%d velocity:%d\n", event->data.note.channel, event->data.note.note, event->data.note.velocity);
#endif
            break;

        case SND_SEQ_EVENT_KEYPRESS:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xA0 | event->data.note.channel | (event->data.note.note << 8) | (event->data.note.velocity << 16));
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Keypress, channel:%d note:%d velocity:%d\n", event->data.note.channel, event->data.note.note, event->data.note.velocity);
#endif
            break;

        case SND_SEQ_EVENT_CONTROLLER:
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (event->data.control.param << 8) | (event->data.control.value << 16));
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Controller, channel:%d param:%d value:%d\n", event->data.control.channel, event->data.control.param, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_PGMCHANGE:
            CWS_MidiMessageShort(event_time, 0xC0 | event->data.control.channel | (event->data.control.value << 8));
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Program change, channel:%d value:%d\n", event->data.control.channel, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_CHANPRESS:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xD0 | event->data.control.channel | (event->data.control.value << 8));
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Channel pressure, channel:%d value:%d\n", event->data.control.channel, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_PITCHBEND:
            CWS_MidiMessageShort(event_time, 0xE0 | event->data.control.channel | (((event->data.control.value + 0x2000) & 0x7F) << 8) | (((event->data.control.value + 0x2000) & 0x3F80) << 9));
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Pitch bend, channel:%d value:%d\n", event->data.control.channel, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_CONTROL14:
            if (event->data.control.param >= 0 && event->data.control.param < 32)
            {
                CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (event->data.control.param << 8) | ((event->data.control.value & 0x3F80) << 9)); // MSB
                CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | ((event->data.control.param + 32) << 8) | ((event->data.control.value & 0x7F) << 16)); // LSB
                midi_event_written = 1;

#ifdef PRINT_EVENTS
                printf("Controller 14-bit, channel:%d param:%d value:%d\n", event->data.control.channel, event->data.control.param, event->data.control.value);
#endif
            }
            else
            {
#ifdef PRINT_EVENTS
                printf("Unknown controller, channel:%d param:%d value:%d\n", event->data.control.channel, event->data.control.param, event->data.control.value);
#endif
            }
            break;

        case SND_SEQ_EVENT_NONREGPARAM:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x63 << 8) | ((event->data.control.param & 0x3F80) << 9)); // NRPN MSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x62 << 8) | ((event->data.control.param & 0x7F) << 16));  // NRPN LSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x06 << 8) | ((event->data.control.value & 0x3F80) << 9)); // data entry MSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x26 << 8) | ((event->data.control.value & 0x7F) << 16));  // data entry LSB
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("NRPN, channel:%d param:%d value:%d\n", event->data.control.channel, event->data.control.param, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_REGPARAM:
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x65 << 8) | ((event->data.control.param & 0x3F80) << 9)); // RPN MSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x64 << 8) | ((event->data.control.param & 0x7F) << 16));  // RPN LSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x06 << 8) | ((event->data.control.value & 0x3F80) << 9)); // data entry MSB
            CWS_MidiMessageShort(event_time, 0xB0 | event->data.control.channel | (0x26 << 8) | ((event->data.control.value & 0x7F) << 16));  // data entry LSB
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("RPN, channel:%d param:%d value:%d\n", event->data.control.channel, event->data.control.param, event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_SYSEX:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageLong(event_time, (uint8_t *)event->data.ext.ptr, event->data.ext.len);
            midi_event_written = 1;
#endif
#ifdef PRINT_EVENTS
            printf("SysEx (fragment) of size %d\n", event->data.ext.len);
#endif
            break;

        case SND_SEQ_EVENT_QFRAME:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF1 | (event->data.control.value << 8));
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("MTC Quarter Frame, value:%d\n", event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_SONGPOS:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF2 | (((event->data.control.value + 0x2000) & 0x7F) << 8) | (((event->data.control.value + 0x2000) & 0x3F80) << 9));
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Song Position, value:%d\n", event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_SONGSEL:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF3 | (event->data.control.value << 8));
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Song Select, value:%d\n", event->data.control.value);
#endif
            break;

        case SND_SEQ_EVENT_TUNE_REQUEST:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF6);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Tune Request\n");
#endif
            break;

        case SND_SEQ_EVENT_CLOCK:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF8);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Clock\n");
#endif
            break;

        case SND_SEQ_EVENT_TICK:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xF9);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Tick\n");
#endif
            break;

        case SND_SEQ_EVENT_START:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xFA);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Start\n");
#endif
            break;

        case SND_SEQ_EVENT_CONTINUE:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xFB);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Continue\n");
#endif
            break;

        case SND_SEQ_EVENT_STOP:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xFC);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Stop\n");
#endif
            break;

        case SND_SEQ_EVENT_SENSING:
            // Not used by CrystalWave Synth
#if 0
            CWS_MidiMessageShort(event_time, 0xFE);
            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
            printf("Active Sense\n");
#endif
            break;

        case SND_SEQ_EVENT_RESET:
            CWS_MidiMessageShort(event_time, 0xFF);
            midi_event_written = 1;

#ifdef PRINT_EVENTS
            printf("Reset\n");
#endif
            break;

        case SND_SEQ_EVENT_PORT_SUBSCRIBED:
            subscription_event(event);
            break;

        case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
            subscription_event(event);
            break;

        default:
            fprintf(stderr, "Unhandled event type: %i\n", event->type);
            break;
    }
}

static void *midi_thread_proc(void *arg)
{
    snd_seq_event_t *event;

    // try setting thread scheduler (only root)
    set_thread_scheduler();

    // set thread as initialized
    *(int *)arg = 1;

    wait_for_midi_initialization();

    while (midi_init_state > 0)
    {
        if (snd_seq_event_input(midi_seq, &event) < 0)
        {
            continue;
        }

        process_event(event);
    }

    return NULL;
}

static void usage(const char *progname)
{
    static const char basename[] = "cws_alsadrv";

    if (progname == NULL)
    {
        progname = basename;
    }
    else
    {
        const char *slash;

        slash = strrchr(progname, '/');
        if (slash != NULL)
        {
            progname = slash + 1;
        }
    }

    printf(
        "%s - CrystalWave Software Synthesizer\n"
        "Usage: %s [OPTIONS]...\n"
        "  -w PATH  Datafile path (path to cwcwtsmp.dat)\n"
        "  -f NUM   Frequency (8000-48000 Hz [default=22050])\n"
        "  -p NUM   Polyphony (8-128 [default=32])\n"
        "  -v NUM   Volume (0-1023 [default=752])\n"
        "  -c NUM   Chorus effect (0=off [default], 1=on)\n"
        "  -r NUM   Reverb effect (0=off [default], 1=small room, 2=large room, 3=custom)\n"
        "  -m NUM   Mode (0 = original, 1 = enhanced [default])\n"
        "  -d       Daemonize\n"
        "  -h       Help\n"
        "Advanced parameters:\n"
        "  -aChorusDepth NUM  Chorus depth (0-100 [default=88])\n"
        "  -aChorusRate NUM   Chorus rate (0-100 [default=66])\n"
        "  -aReverbGain0 FLT  Coefficient for first tap delay [default=0.9]\n"
        "  -aReverbGain1 FLT  Coefficient for second tap delay [default=0.9]\n"
        "  -aReverbGain2 FLT  Coefficient for third tap delay [default=0.3]\n"
        "  -aReverbGain3 FLT  Coefficient for fourth tap delay [default=0.3]\n"
        "  -aReverbDel0 FLT   Delay of first tap [default=0.0113]\n"
        "  -aReverbDel1 FLT   Delay of second tap [default=0.0127]\n"
        "  -aReverbDel2 FLT   Delay of third tap [default=0.0372]\n"
        "  -aReverbDel3 FLT   Delay of fourth tap [default=0.0443]\n"
        "  -aReverbFc FLT     Low pass filter coefficient [default=0.78]\n"
        "  -aReverbFwd FLT    Mix level coefficient [default=0.5]\n",
        basename,
        progname
    );
    exit(1);
}

static void read_arguments(int argc, char *argv[]) __attribute__((noinline));
static void read_arguments(int argc, char *argv[])
{
    int i, j;
    double k;

    // basic settings
    frequency = 22050;
    polyphony = 32;
    volume = 752;
    chorus_effect = 0;
    reverb_effect = 0;
    enhanced_mode = 1;
    daemonize = 0;

    // advanced settings
    chorus_depth = 88;
    chorus_rate = 60;
    reverb_gain[0] = 0.900f;
    reverb_gain[1] = 0.900f;
    reverb_gain[2] = 0.300f;
    reverb_gain[3] = 0.300f;
    reverb_del[0] = 0.0113f;
    reverb_del[1] = 0.0127f;
    reverb_del[2] = 0.0372f;
    reverb_del[3] = 0.0443f;
    reverb_fc = 0.780f;
    reverb_fwd = 0.500f;

    if (argc <= 1)
    {
        return;
    }

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] != 0 && argv[i][2] == 0)
        {
            switch (argv[i][1])
            {
                case 'w': // data file
                    if ((i + 1) < argc)
                    {
                        i++;
                        data_filepath = argv[i];
                    }
                    break;
                case 'f': // frequency
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 8000 && j <= 48000)
                        {
                            frequency = j;
                        }
                    }
                    break;
                case 'p': // polyphony
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 8 && j <= 128)
                        {
                            polyphony = j;
                        }
                    }
                    break;
                case 'v': // volume
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 0 && j <= 1023)
                        {
                            volume = j;
                        }
                    }
                    break;
                case 'c': // chorus effect
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 0 && j <= 1)
                        {
                            chorus_effect = j;
                        }
                    }
                    break;
                case 'r': // reverb effect
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 0 && j <= 3)
                        {
                            reverb_effect = j;
                        }
                    }
                    break;
                case 'm': // mode
                    if ((i + 1) < argc)
                    {
                        i++;
                        j = atoi(argv[i]);
                        if (j >= 0 && j <= 1)
                        {
                            enhanced_mode = j;
                        }
                    }
                    break;
                case 'd': // daemonize
                    daemonize = 1;
                    break;
                case 'h': // help
                    usage(argv[0]);
                default:
                    break;
            }
        }
        else if (argv[i][0] == '-' && argv[i][1] == 'a')
        {
            if (0 == strcmp(argv[i] + 2, "ChorusDepth"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    j = atoi(argv[i]);
                    if (j >= 0 && j <= 100)
                    {
                        chorus_depth = j;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ChorusRate"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    j = atoi(argv[i]);
                    if (j >= 0 && j <= 100)
                    {
                        chorus_rate = j;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbGain0"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_gain[0] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbGain1"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_gain[1] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbGain2"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_gain[2] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbGain3"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_gain[3] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbDel0"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_del[0] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbDel1"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_del[1] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbDel2"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_del[2] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbDel3"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_del[3] = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbFc"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_fc = (float)k;
                    }
                }
            }
            else if (0 == strcmp(argv[i] + 2, "ReverbFwd"))
            {
                if ((i + 1) < argc)
                {
                    i++;
                    k = atof(argv[i]);
                    if (k >= 0.0)
                    {
                        reverb_fwd = (float)k;
                    }
                }
            }
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            usage(argv[0]);
        }
    }
}


static int load_data_file(void)
{
    int data_fd;
    off_t datalen;

    data_fd = open(data_filepath, O_RDONLY);
    if (data_fd < 0)
    {
        char *pathcopy, *slash, *filename;
        DIR *dir;
        struct dirent *entry;

        pathcopy = strdup(data_filepath);
        if (pathcopy == NULL) return -1;

        slash = strrchr(pathcopy, '/');
        if (slash != NULL)
        {
            filename = slash + 1;
            if (slash != pathcopy)
            {
                *slash = 0;
                dir = opendir(pathcopy);
                *slash = '/';
            }
            else
            {
                dir = opendir("/");
            }
        }
        else
        {
            filename = pathcopy;
            dir = opendir(".");
        }

        if (dir == NULL)
        {
            free(pathcopy);
            return -2;
        }

        while (1)
        {
            entry = readdir(dir);
            if (entry == NULL) break;

            if (entry->d_type != DT_UNKNOWN && entry->d_type != DT_REG && entry->d_type != DT_LNK) continue;

            if (0 != strcasecmp(filename, entry->d_name)) continue;

            strcpy(filename, entry->d_name);
            break;
        };

        closedir(dir);

        if (entry == NULL)
        {
            free(pathcopy);
            return -3;
        }

        data_fd = open(pathcopy, O_RDONLY);

        free(pathcopy);

        if (data_fd < 0)
        {
            return -4;
        }
    }

    datalen = lseek(data_fd, 0, SEEK_END);
    if (datalen <= 4)
    {
        close(data_fd);
        return -5;
    }

    if (0 != lseek(data_fd, 0, SEEK_SET))
    {
        close(data_fd);
        return -6;
    }

#if defined(MAP_LOCKED) && (MAP_LOCKED != 0)
    datafile_ptr = (uint8_t *)mmap(NULL, datalen, PROT_READ, MAP_PRIVATE | MAP_LOCKED, data_fd, 0);
    if (datafile_ptr == MAP_FAILED)
#endif
    {
        datafile_ptr = (uint8_t *)mmap(NULL, datalen, PROT_READ, MAP_PRIVATE, data_fd, 0);
    }

    close(data_fd);

    if (datafile_ptr == MAP_FAILED)
    {
        return -5;
    }

    datafile_len = datalen;

    return 0;
}


static int start_synth(void) __attribute__((noinline));
static int start_synth(void)
{
    int remaining, index;

    if (load_data_file() < 0)
    {
        fprintf(stderr, "Error opening DATA file: %s\n", data_filepath);
        return -1;
    }

    cws_event_buffer = &event_buffer;

    CWS_InitializeSynth(datafile_ptr, cws_event_buffer, chorus_effect, chorus_depth, chorus_rate, volume, polyphony, frequency, enhanced_mode);

    if (reverb_effect == 1 || reverb_effect == 2)
    {
        CWS_SetReverbPreset(reverb_effect - 1, frequency);
    }
    else if (reverb_effect == 3)
    {
        CWS_SetReverbCustom(reverb_gain[0], reverb_gain[1], reverb_gain[2], reverb_gain[3], reverb_del[0], reverb_del[1], reverb_del[2], reverb_del[3], reverb_fc, reverb_fwd, frequency);
    }

    // prepare output buffer
    memset(output_buffer, 0, 4 * frequency);
    output_counter = 0;
    samples_per_call = frequency / 100;
    remaining = frequency;
    for (index = 0; index < 100; index++)
    {
        output_ptr[index] = output_buffer + 2 * (frequency - remaining);
        output_num_samples[index] = (int)(((float)remaining / (float)(100 - index)) + 0.5f);
        remaining -= output_num_samples[index];
    }


    // initialize time
#if defined(CLOCK_MONOTONIC_RAW)
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &start_time))
    {
        monotonic_clock_id = CLOCK_MONOTONIC;
        clock_gettime(CLOCK_MONOTONIC, &start_time);
    }
    else
    {
        monotonic_clock_id = CLOCK_MONOTONIC_RAW;
    }
#else
    clock_gettime(CLOCK_MONOTONIC, &start_time);
#endif

    if (start_time.tv_nsec >= 35000000)
    {
        start_time.tv_nsec -= 35000000; // 35 ms
    }
    else
    {
        start_time.tv_sec--;
        start_time.tv_nsec += 1000000000 - 35000000;
    }


    return 0;
}

static void stop_synth(void)
{
    munmap(datafile_ptr, datafile_len);
}

static int run_as_daemon(void) __attribute__((noinline));
static int run_as_daemon(void)
{
    int err;

    printf("Running as daemon...\n");

    err = daemon(0, 0);
    if (err < 0)
    {
        fprintf(stderr, "Error running as daemon: %i\n", err);
        return -1;
    }

    return 0;
}

static int open_midi_port(void) __attribute__((noinline));
static int open_midi_port(void)
{
    int err;
    unsigned int caps, type;

    err = snd_seq_open(&midi_seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
    if (err < 0)
    {
        fprintf(stderr, "Error opening ALSA sequencer: %i\n%s\n", err, snd_strerror(err));
        return -1;
    }

    err = snd_seq_set_client_name(midi_seq, midi_name);
    if (err < 0)
    {
        snd_seq_close(midi_seq);
        fprintf(stderr, "Error setting sequencer client name: %i\n%s\n", err, snd_strerror(err));
        return -2;
    }

    caps = SND_SEQ_PORT_CAP_SUBS_WRITE | SND_SEQ_PORT_CAP_WRITE;
    type = SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_MIDI_GM | SND_SEQ_PORT_TYPE_SYNTHESIZER;
    err = snd_seq_create_simple_port(midi_seq, port_name, caps, type);
    if (err < 0)
    {
        snd_seq_close(midi_seq);
        fprintf(stderr, "Error creating sequencer port: %i\n%s\n", err, snd_strerror(err));
        return -3;
    }
    midi_port_id = err;

    printf("%s ALSA address is %i:0\n", midi_name, snd_seq_client_id(midi_seq));

    return 0;
}

static void close_midi_port(void)
{
    snd_seq_delete_port(midi_seq, midi_port_id);
    snd_seq_close(midi_seq);
}


static int set_hw_params(void)
{
    int err, dir;
    unsigned int rate;
    snd_pcm_uframes_t buffer_size, period_size;
    snd_pcm_hw_params_t *pcm_hwparams;

    snd_pcm_hw_params_alloca(&pcm_hwparams);

    err = snd_pcm_hw_params_any(midi_pcm, pcm_hwparams);
    if (err < 0)
    {
        fprintf(stderr, "Error getting hwparams: %i\n%s\n", err, snd_strerror(err));
        return -1;
    }

    err = snd_pcm_hw_params_set_access(midi_pcm, pcm_hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (err < 0)
    {
        fprintf(stderr, "Error setting access: %i\n%s\n", err, snd_strerror(err));
        return -2;
    }

    err = snd_pcm_hw_params_set_format(midi_pcm, pcm_hwparams, SND_PCM_FORMAT_S16);
    if (err < 0)
    {
        fprintf(stderr, "Error setting format: %i\n%s\n", err, snd_strerror(err));
        return -3;
    }

    err = snd_pcm_hw_params_set_channels(midi_pcm, pcm_hwparams, 2);
    if (err < 0)
    {
        fprintf(stderr, "Error setting channels: %i\n%s\n", err, snd_strerror(err));
        return -4;
    }

    rate = frequency;
    dir = 0;
    err = snd_pcm_hw_params_set_rate_near(midi_pcm, pcm_hwparams, &rate, &dir);
    if (err < 0)
    {
        fprintf(stderr, "Error setting rate: %i\n%s\n", err, snd_strerror(err));
        return -5;
    }

    buffer_size = frequency;
    err = snd_pcm_hw_params_set_buffer_size_near(midi_pcm, pcm_hwparams, &buffer_size);
    if (err < 0)
    {
        fprintf(stderr, "Error setting buffer size: %i\n%s\n", err, snd_strerror(err));
        return -6;
    }

    period_size = samples_per_call;
    dir = 0;
    err = snd_pcm_hw_params_set_period_size_near(midi_pcm, pcm_hwparams, &period_size, &dir);
    if (err < 0)
    {
        fprintf(stderr, "Error setting period size: %i\n%s\n", err, snd_strerror(err));
        return -7;
    }

    err = snd_pcm_hw_params(midi_pcm, pcm_hwparams);
    if (err < 0)
    {
        fprintf(stderr, "Error setting hwparams: %i\n%s\n", err, snd_strerror(err));
        return -8;
    }

    return 0;
}

static int set_sw_params(void)
{
    snd_pcm_sw_params_t *swparams;
    int err;

    snd_pcm_sw_params_alloca(&swparams);

    err = snd_pcm_sw_params_current(midi_pcm, swparams);
    if (err < 0)
    {
        fprintf(stderr, "Error getting swparams: %i\n%s\n", err, snd_strerror(err));
        return -1;
    }

    err = snd_pcm_sw_params_set_avail_min(midi_pcm, swparams, samples_per_call);
    if (err < 0)
    {
        fprintf(stderr, "Error setting avail min: %i\n%s\n", err, snd_strerror(err));
        return -2;
    }

    err = snd_pcm_sw_params(midi_pcm, swparams);
    if (err < 0)
    {
        fprintf(stderr, "Error setting sw params: %i\n%s\n", err, snd_strerror(err));
        return -3;
    }

    return 0;
}

static int open_pcm_output(void) __attribute__((noinline));
static int open_pcm_output(void)
{
    int err;

    err = snd_pcm_open(&midi_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
    {
        fprintf(stderr, "Error opening PCM device: %i\n%s\n", err, snd_strerror(err));
        return -1;
    }

    if (set_hw_params() < 0)
    {
        return -2;
    }

    if (set_sw_params() < 0)
    {
        return -3;
    }

    // set nonblock mode
    snd_pcm_nonblock(midi_pcm, 1);

    snd_pcm_prepare(midi_pcm);

    return 0;
}

static void close_pcm_output(void)
{
    snd_pcm_close(midi_pcm);
}


static int drop_privileges(void)
{
    uid_t uid;
    gid_t gid;
    const char *sudo_id;
    long long int llid;
    const char *xdg_dir;
    char buf[32];
    struct stat statbuf;
    struct passwd *passwdbuf;

    if (getuid() != 0)
    {
        return 0;
    }

    sudo_id = secure_getenv("SUDO_UID");
    if (sudo_id == NULL)
    {
        sudo_id = secure_getenv("PKEXEC_UID");
        if (sudo_id == NULL)
        {
            return -1;
        }
    }

    errno = 0;
    llid = strtoll(sudo_id, NULL, 10);
    uid = (uid_t) llid;
    if (errno != 0 || uid == 0 || llid != (long long int)uid)
    {
        return -2;
    }

    gid = getgid();
    if (gid == 0)
    {
        sudo_id = secure_getenv("SUDO_GID");
        if (sudo_id == NULL)
        {
            passwdbuf = getpwuid(uid);
            if (passwdbuf != NULL)
            {
                gid = passwdbuf->pw_gid;
            }

            if (gid == 0)
            {
                return -3;
            }
        }
        else
        {
            errno = 0;
            llid = strtoll(sudo_id, NULL, 10);
            gid = (gid_t) llid;
            if (errno != 0 || gid == 0 || llid != (long long int)gid)
            {
                return -4;
            }
        }
    }

    if (setgid(gid) != 0)
    {
        return -5;
    }
    if (setuid(uid) != 0)
    {
        return -6;
    }

    printf("Dropped root privileges\n");

    if (0 != chdir("/")) {}

    // define some environment variables

    xdg_dir = getenv("XDG_RUNTIME_DIR");
    if ((xdg_dir == NULL) || (*xdg_dir == 0))
    {
        snprintf(buf, 32, "/run/user/%lli", (long long int)uid);

        if ((stat(buf, &statbuf) == 0) && ((statbuf.st_mode & S_IFMT) == S_IFDIR) && (statbuf.st_uid == uid))
        {
            // if XDG_RUNTIME_DIR is not defined and directory /run/user/$USER exists then use it for XDG_RUNTIME_DIR
            setenv("XDG_RUNTIME_DIR", buf, 1);

            xdg_dir = getenv("XDG_CONFIG_HOME");
            if ((xdg_dir == NULL) || (*xdg_dir == 0))
            {
                passwdbuf = getpwuid(uid);
                if (passwdbuf != NULL)
                {
                    // also if XDG_CONFIG_HOME is not defined then define it as user's home directory
                    setenv("XDG_CONFIG_HOME", passwdbuf->pw_dir, 1);
                }
            }
        }
    }

    return 0;
}

static int start_thread(void) __attribute__((noinline));
static int start_thread(void)
{
    pthread_attr_t attr;
    int err;
    volatile int initialized;

    // try to increase priority (only root)
    if (-1 == nice(-20)) {}

    err = pthread_attr_init(&attr);
    if (err != 0)
    {
        fprintf(stderr, "Error creating thread attribute: %i\n", err);
        return -1;
    }

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    midi_init_state = 0;
    initialized = 0;
    err = pthread_create(&midi_thread, &attr, &midi_thread_proc, (void *)&initialized);
    pthread_attr_destroy(&attr);

    if (err != 0)
    {
        fprintf(stderr, "Error creating thread: %i\n", err);
        return -2;
    }

    // wait for thread initialization
    while (initialized == 0)
    {
        struct timespec req;

        req.tv_sec = 0;
        req.tv_nsec = 10000000;
        nanosleep(&req, NULL);
    };

    if (drop_privileges() < 0)
    {
        fprintf(stderr, "Error dropping root privileges\n");
    }

    return 0;
}


static int output_subbuffer(int num)
{
    snd_pcm_uframes_t remaining;
    snd_pcm_sframes_t written;
    int16_t *buf_ptr;

    remaining = output_num_samples[num];
    buf_ptr = output_ptr[num];

    while (remaining)
    {
        written = snd_pcm_writei(midi_pcm, buf_ptr, remaining);
        if (written < 0)
        {
            return -1;
        }

        remaining -= written;
        buf_ptr += 2 * written;
    };

    return 0;
}

static void main_loop(void) __attribute__((noinline));
static void main_loop(void)
{
    int is_paused;
    struct timespec last_written_time, current_time;

    for (int i = 2; i < 100; i++)
    {
        output_subbuffer(i);
    }

    is_paused = 0;
    // pause pcm playback at the beginning
    if (0 == snd_pcm_pause(midi_pcm, 1))
    {
        is_paused = 1;
        printf("PCM playback paused\n");
    }
    else
    {
        // if pausing doesn't work then set time of last written event as current time, so the next attempt to pause will be in 60 seconds
        clock_gettime(MONOTONIC_CLOCK_TYPE, &last_written_time);
    }

    midi_event_written = 0;
    midi_init_state = 1;

    for (;;)
    {
        struct timespec req;
        snd_pcm_state_t pcmstate;
        snd_pcm_sframes_t available_frames;

        req.tv_sec = 0;
        req.tv_nsec = 10000000;
        nanosleep(&req, NULL);

        if (midi_event_written)
        {
            midi_event_written = 0;

            // remember time of last written event
            clock_gettime(MONOTONIC_CLOCK_TYPE, &last_written_time);

            if (is_paused)
            {
                is_paused = 0;
                snd_pcm_pause(midi_pcm, 0);
                printf("PCM playback unpaused\n");
            }
        }
        else
        {
            if (is_paused)
            {
                continue;
            }

            clock_gettime(MONOTONIC_CLOCK_TYPE, &current_time);
            // if more than 60 seconds elapsed from last written event, then pause pcm playback
            if (current_time.tv_sec - last_written_time.tv_sec > 60)
            {
                if (0 == snd_pcm_pause(midi_pcm, 1))
                {
                    is_paused = 1;
                    printf("PCM playback paused\n");
                    continue;
                }
                else
                {
                    // if pausing doesn't work then set time of last written event as current time, so the next attempt to pause will be in 60 seconds
                    last_written_time = current_time;
                }
            }
        }

        pcmstate = snd_pcm_state(midi_pcm);
        if (pcmstate == SND_PCM_STATE_XRUN)
        {
            fprintf(stderr, "Buffer underrun\n");
            snd_pcm_prepare(midi_pcm);
        }

        available_frames = snd_pcm_avail_update(midi_pcm);
        while (available_frames >= (3 * samples_per_call))
        {
            CWS_FillOutputBuffer(output_ptr[output_counter], 4 * output_num_samples[output_counter], CWS_GetTime());

            if (reverb_effect)
            {
                CWS_ApplyReverb(output_ptr[output_counter], 4 * output_num_samples[output_counter]);
            }

            if (output_subbuffer(output_counter) < 0)
            {
                fprintf(stderr, "Error writing audio data\n");
                available_frames = 0;
                break;
            }
            else
            {
                available_frames -= output_num_samples[output_counter];
            }

            output_counter++;
            if (output_counter >= 100) output_counter = 0;
        };
    };
}

int main(int argc, char *argv[])
{
    read_arguments(argc, argv);

    if (start_synth() < 0)
    {
        return 2;
    }

    if (daemonize)
    {
        if (run_as_daemon() < 0)
        {
            stop_synth();
            return 3;
        }
    }

    if (start_thread() < 0)
    {
        stop_synth();
        return 4;
    }

    if (open_pcm_output() < 0)
    {
        midi_init_state = -1;
        stop_synth();
        return 5;
    }

    if (open_midi_port() < 0)
    {
        midi_init_state = -1;
        close_pcm_output();
        stop_synth();
        return 6;
    }

    main_loop();

    midi_init_state = -1;
    close_midi_port();
    close_pcm_output();
    stop_synth();
    return 0;
}

