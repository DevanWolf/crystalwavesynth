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

#include <AvailabilityMacros.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mman.h>
#include <pwd.h>
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <crt_externs.h>
#include "cwssynth.h"

#if defined(MAC_OS_VERSION_11_0) && MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_11_0
#if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_VERSION_11_0
#define MIDI_API 0
#else
#define MIDI_API 1
#endif
#else
#define MIDI_API -1
#endif


#define MIDI_NAME "CrystalWave Synth"

static const uint32_t midi_name_crc32 = 0x6d9b2f6a;

static MIDIClientRef midi_client;
static MIDIEndpointRef midi_endpoint;
static AudioQueueRef midi_pcm_queue;
static volatile int midi_event_written;

static int frequency, polyphony, volume, chorus_effect, reverb_effect, enhanced_mode, daemonize;
static int chorus_depth, chorus_rate;
static float reverb_gain[4], reverb_del[4], reverb_fc, reverb_fwd;
static const char *data_filepath = "cwcwtsmp.dat";

static int datafile_len;
static uint8_t *datafile_ptr;
static event_buffer_t event_buffer;
static struct AudioQueueBuffer *midi_queue_buffer[100];
static uint16_t output_num_samples[100];
static int output_counter;

#ifdef __cplusplus
extern "C" {
#endif

event_buffer_t *cws_event_buffer;

#ifdef __cplusplus
}
#endif

static uint64_t start_time;


static uint32_t CWS_GetTime(void)
{
    return (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - start_time) / 1000000u;
}


#if (MIDI_API <= 0)
static void midi_read_proc(const MIDIPacketList *pktlist, void * __nullable readProcRefCon, void * __nullable srcConnRefCon)
{
    unsigned int index1, index2, length, time;
    const MIDIPacket *packet;

    for (index1 = 0, packet = pktlist->packet; index1 < pktlist->numPackets; index1++, packet = MIDIPacketNext(packet))
    {
        time = CWS_GetTime();

        for (index2 = 0; index2 < packet->length; index2++)
        {
            switch ((packet->data[index2] >> 4) & 0x0f) // status
            {
                case 0x08:
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                    midi_event_written = 1;

#ifdef PRINT_EVENTS
                    printf("Note OFF, channel:%d note:%d velocity:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f, packet->data[index2 + 2] & 0x7f);
#endif

                    index2 += 2;
                    break;

                case 0x09:
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                    midi_event_written = 1;

#ifdef PRINT_EVENTS
                    printf("Note ON, channel:%d note:%d velocity:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f, packet->data[index2 + 2] & 0x7f);
#endif

                    index2 += 2;
                    break;

                case 0x0a:
                    // Not used by CrystalWave Synth
#if 0
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                    midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                    printf("Keypress, channel:%d note:%d velocity:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f, packet->data[index2 + 2] & 0x7f);
#endif

                    index2 += 2;
                    break;

                case 0x0b:
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                    midi_event_written = 1;

#ifdef PRINT_EVENTS
                    printf("Controller, channel:%d param:%d value:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f, packet->data[index2 + 2] & 0x7f);
#endif

                    index2 += 2;
                    break;

                case 0x0c:
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8));
                    midi_event_written = 1;

#ifdef PRINT_EVENTS
                    printf("Program change, channel:%d value:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f);
#endif

                    index2++;
                    break;

                case 0x0d:
                    // Not used by CrystalWave Synth
#if 0
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8));
                    midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                    printf("Channel pressure, channel:%d value:%d\n", packet->data[index2] & 0x0f, packet->data[index2 + 1] & 0x7f);
#endif

                    index2++;
                    break;

                case 0x0e:
                    CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                    midi_event_written = 1;

#ifdef PRINT_EVENTS
                    printf("Pitch bend, channel:%d value:%d\n", packet->data[index2] & 0x0f, ((packet->data[index2 + 1] & 0x7f) | ((packet->data[index2 + 2] & 0x7f) << 7)) - 0x2000);
#endif

                    index2 += 2;
                    break;

                case 0x0f:
                    switch (packet->data[index2])
                    {
                        case 0xf0:
                            length = packet->length - index2;

                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageLong(time, packet->data + index2, length);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("SysEx (fragment), length:%d\n", length);
#endif

                            index2 = packet->length - 1;
                            break;

                        case 0xf1:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8));
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("MTC Quarter Frame, value:%d\n", packet->data[index2 + 1] & 0x7f);
#endif

                            index2++;
                            break;

                        case 0xf2:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8) | ((packet->data[index2 + 2] & 0x7f) << 16));
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Song Position, value:%d\n", ((packet->data[index2 + 1] & 0x7f) | ((packet->data[index2 + 2] & 0x7f) << 7)) - 0x2000);
#endif

                            index2 += 2;
                            break;

                        case 0xf3:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2] | ((packet->data[index2 + 1] & 0x7f) << 8));
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Song Select, value:%d\n", packet->data[index2 + 1] & 0x7f);
#endif

                            index2++;
                            break;

                        case 0xf6:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Tune Request\n");
#endif

                            break;

                        case 0xf8:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Clock\n");
#endif

                            break;

                        case 0xfa:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Start\n");
#endif

                            break;

                        case 0xfb:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Continue\n");
#endif

                            break;

                        case 0xfc:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Stop\n");
#endif

                            break;

                        case 0xfe:
                            // Not used by CrystalWave Synth
#if 0
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                            printf("Active Sense\n");
#endif

                            break;

                        case 0xff:
                            CWS_MidiMessageShort(time, packet->data[index2]);
                            midi_event_written = 1;

#ifdef PRINT_EVENTS
                            printf("Reset\n");
#endif

                            break;

                        default:
                            fprintf(stderr, "Unhandled system message: 0x%x\n", packet->data[index2]);
                            break;
                    }
                    break;
                default:
                    if (index2 == 0)
                    {
                        length = packet->length;

                        // Not used by CrystalWave Synth
#if 0
                        CWS_MidiMessageLong(time, packet->data, length);
                        midi_event_written = 1;
#endif

#ifdef PRINT_EVENTS
                        printf("SysEx (fragment) of size %d\n", length);
#endif

                        index2 = packet->length - 1;
                    }
                    else
                    {
                        fprintf(stderr, "Unhandled message: 0x%x\n", packet->data[index2]);
                    }
                    break;
            }
        }
    }
}
#endif

#if (MIDI_API >= 0)
static void midi_receive_proc(const MIDIEventList *evtlist, void * __nullable srcConnRefCon)
{
    unsigned int index1, index2, time;
    const MIDIEventPacket *packet;

    for (index1 = 0, packet = evtlist->packet; index1 < evtlist->numPackets; index1++, packet = MIDIEventPacketNext(packet))
    {
        time = CWS_GetTime();

        for (index2 = 0; index2 < packet->wordCount; index2++)
        {
            switch ((packet->words[index2] >> 28) & 0x0f) // message type
            {
                // Traditional MIDI 1.0 Functionality
                case kMIDIMessageTypeSystem:
                    switch ((packet->words[index2] >> 16) & 0xff) // status
                    {
                        case kMIDIStatusMTC:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xF1 | (packet->words[index2] & 0x7f00));
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("MTC Quarter Frame, group:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 8) & 0x7f);
#endif

                            break;

                        case kMIDIStatusSongPosPointer:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xF2 | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Song Position, group:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (((packet->words[index2] >> 8) & 0x7f) | ((packet->words[index2] & 0x7f) << 7)) - 0x2000);
#endif

                            break;

                        case kMIDIStatusSongSelect:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xF3 | (packet->words[index2] & 0x7f00));
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Song Select, group:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 8) & 0x7f);
#endif

                            break;

                        case kMIDIStatusTuneRequest:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xF6);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Tune Request, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusTimingClock:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xF8);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Clock, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusStart:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xFA);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Start, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusContinue:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xFB);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Continue, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusStop:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xFC);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Stop, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusActiveSending:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xFE);
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Active Sense, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        case kMIDIStatusSystemReset:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, 0xFF);
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Reset, group:%d\n", (packet->words[index2] >> 24) & 0x0f);
#endif

                            break;

                        default:
                            fprintf(stderr, "Unhandled system message: 0x%x, group:%d\n", (packet->words[index2] >> 16) & 0xff, (packet->words[index2] >> 24) & 0x0f);
                            break;
                    }
                    break;

                case kMIDIMessageTypeChannelVoice1:
                    switch ((packet->words[index2] >> 20) & 0x0f) // status
                    {
                        case kMIDICVStatusNoteOff:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Note OFF, group:%d channel:%d note:%d velocity:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f, packet->words[index2] & 0x7f);
#endif

                            break;

                        case kMIDICVStatusNoteOn:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Note ON, group:%d channel:%d note:%d velocity:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f, packet->words[index2] & 0x7f);
#endif

                            break;

                        case kMIDICVStatusPolyPressure:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Keypress, group:%d channel:%d note:%d velocity:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f, packet->words[index2] & 0x7f);
#endif

                            break;

                        case kMIDICVStatusControlChange:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Controller, group:%d channel:%d param:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f, packet->words[index2] & 0x7f);
#endif

                            break;

                        case kMIDICVStatusProgramChange:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00));
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Program change, group:%d channel:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f);
#endif

                            break;

                        case kMIDICVStatusChannelPressure:
                            // Not used by CrystalWave Synth
#if 0
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00));
                                midi_event_written = 1;
                            }
#endif

#ifdef PRINT_EVENTS
                            printf("Channel pressure, group:%d channel:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (packet->words[index2] >> 8) & 0x7f);
#endif

                            break;

                        case kMIDICVStatusPitchBend:
                            if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                            {
                                CWS_MidiMessageShort(time, ((packet->words[index2] >> 16) & 0xff) | (packet->words[index2] & 0x7f00) | ((packet->words[index2] & 0x7f) << 16));
                                midi_event_written = 1;
                            }

#ifdef PRINT_EVENTS
                            printf("Pitch bend, group:%d channel:%d value:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f, (((packet->words[index2] >> 8) & 0x7f) | ((packet->words[index2] & 0x7f) << 7)) - 0x2000);
#endif

                            break;

                        default:
                            fprintf(stderr, "Unhandled channel voice message: 0x%x, group:%d\n", (packet->words[index2] >> 16) & 0xff, (packet->words[index2] >> 24) & 0x0f);
                            break;
                    }
                    break;

                case kMIDIMessageTypeSysEx:
                    // Not used by CrystalWave Synth
#if 0
                    if (((packet->words[index2] >> 24) & 0x0f) == 0) // group
                    {
                        length = (packet->words[index2] >> 16) & 0x0f;
                        if (length <= 6)
                        {
                            data[1] = (packet->words[index2] >> 8) & 0x7f;
                            data[2] = packet->words[index2] & 0x7f;
                            data[3] = (packet->words[index2 + 1] >> 24) & 0x7f;
                            data[4] = (packet->words[index2 + 1] >> 16) & 0x7f;
                            data[5] = (packet->words[index2 + 1] >> 8) & 0x7f;
                            data[6] = packet->words[index2 + 1] & 0x7f;
                        }
                        else length = 0;

                        running_status = 0;
                        if (((packet->words[index2] >> 20) & 0x0f) == kMIDISysExStatusComplete || ((packet->words[index2] >> 20) & 0x0f) == kMIDISysExStatusEnd)
                        {
                            length++;
                            data[length] = 0xf7;
                        }
                        if (((packet->words[index2] >> 20) & 0x0f) == kMIDISysExStatusComplete || ((packet->words[index2] >> 20) & 0x0f) == kMIDISysExStatusStart)
                        {
                            CWS_MidiMessageLong(time, data, length + 1);
                            midi_event_written = 1;
                        }
                        else
                        {
                            CWS_MidiMessageLong(time, data + 1, length);
                            midi_event_written = 1;
                        }
                    }
#endif

#ifdef PRINT_EVENTS
                    printf("SysEx (fragment), group:%d length:%d\n", (packet->words[index2] >> 24) & 0x0f, (packet->words[index2] >> 16) & 0x0f);
#endif

                    index2++;
                    break;

                // other types - 1 word
                case kMIDIMessageTypeUtility:
                case 6:
                case 7:
                default:
                    fprintf(stderr, "Unhandled message type: %d\n", (packet->words[index2] >> 28) & 0x0f);
                    break;

                // other types - 2 words
                case kMIDIMessageTypeChannelVoice2:
                case 8:
                case 9:
                case 10:
                    fprintf(stderr, "Unhandled message type: %d\n", (packet->words[index2] >> 28) & 0x0f);
                    index2++;
                    break;

                // other types - 3 words
                case 11:
                case 12:
                    fprintf(stderr, "Unhandled message type: %d\n", (packet->words[index2] >> 28) & 0x0f);
                    index2 += 2;
                    break;

                // other types - 4 words
                case kMIDIMessageTypeData128:
                case 13:
                case 14:
                case 15:
                    fprintf(stderr, "Unhandled message type: %d\n", (packet->words[index2] >> 28) & 0x0f);
                    index2 += 3;
                    break;
            }
        }
    }
}
#endif


static void usage(const char *progname)
{
    static const char basename[] = "cws_coredrv";

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
        "  -f NUM   Frequency (8000-48000 Hz [default=44100])\n"
        "  -p NUM   Polyphony (8-128 [default=128])\n"
        "  -v NUM   Volume (0-1023 [default=192])\n"
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
    frequency = 44100;
    polyphony = 128;
    volume = 192;
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
                        reverb_gain[0] = k;
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
                        reverb_gain[1] = k;
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
                        reverb_gain[2] = k;
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
                        reverb_gain[3] = k;
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
                        reverb_del[0] = k;
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
                        reverb_del[1] = k;
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
                        reverb_del[2] = k;
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
                        reverb_del[3] = k;
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
                        reverb_fc = k;
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
                        reverb_fwd = k;
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


static int run_as_daemon_start(char *argv[]) __attribute__((noinline));
static int run_as_daemon_start(char *argv[])
{
    pid_t pid;
    int res, err;
    struct sigaction chld_action;
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];

    // get current signal action for SIGCHLD
    sigaction(SIGCHLD, NULL, &chld_action);

    pid = getpid();
    if (chld_action.sa_handler != SIG_IGN || getpgrp() == pid)
    {
        // spawn a new process (current process is process group leader or signal handler for SIGCHLD is not SIG_IGN)

        signal(SIGCHLD, SIG_IGN);

        res = proc_pidpath(pid, pathbuf, sizeof(pathbuf));
        err = posix_spawn(NULL, (res > 0 && res < sizeof(pathbuf)) ? pathbuf : argv[0], NULL, NULL, argv, *_NSGetEnviron());
        if (err != 0)
        {
            fprintf(stderr, "Error spawning process: %i\n", err);
            return -1;
        }

        exit(0);
    }
    else
    {
        // create new session (current process is not process group leader and signal handler for SIGCHLD is SIG_IGN)

        if (setsid() < 0)
        {
            fprintf(stderr, "Error creating session\n");
            return -2;
        }
    }

    printf("Running as daemon...\n");

    return 0;
}

static void run_as_daemon_finish(void) __attribute__((noinline));
static void run_as_daemon_finish(void)
{
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
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

    datafile_ptr = (uint8_t *)mmap(NULL, datalen, PROT_READ, MAP_PRIVATE, data_fd, 0);

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
    output_counter = 0;
    remaining = frequency;
    for (index = 0; index < 100; index++)
    {
        output_num_samples[index] = (int)(((float)remaining / (float)(100 - index)) + 0.5f);
        remaining -= output_num_samples[index];
    }


    // initialize time
    start_time = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    start_time -= 35000000; // 35 ms


    return 0;
}

static void stop_synth(void) __attribute__((noinline));
static void stop_synth(void)
{
    munmap(datafile_ptr, datafile_len);
}

static int drop_privileges(void)
{
    uid_t uid;
    gid_t gid;
    const char *sudo_id;
    long long int llid;
    struct passwd *passwdbuf;

    if (getuid() != 0)
    {
        return 0;
    }

    if (issetugid())
    {
        return -1;
    }

    sudo_id = getenv("SUDO_UID");
    if (sudo_id == NULL)
    {
        return -2;
    }

    errno = 0;
    llid = strtoll(sudo_id, NULL, 10);
    uid = (uid_t) llid;
    if (errno != 0 || uid == 0 || llid != (long long int)uid)
    {
        return -3;
    }

    gid = getgid();
    if (gid == 0)
    {
        sudo_id = getenv("SUDO_GID");
        if (sudo_id == NULL)
        {
            passwdbuf = getpwuid(uid);
            if (passwdbuf != NULL)
            {
                gid = passwdbuf->pw_gid;
            }

            if (gid == 0)
            {
                return -4;
            }
        }
        else
        {
            errno = 0;
            llid = strtoll(sudo_id, NULL, 10);
            gid = (gid_t) llid;
            if (errno != 0 || gid == 0 || llid != (long long int)gid)
            {
                return -5;
            }
        }
    }

    if (setgid(gid) != 0)
    {
        return -6;
    }
    if (setuid(uid) != 0)
    {
        return -7;
    }

    printf("Dropped root privileges\n");

    chdir("/");

    return 0;
}

static void handle_privileges(void) __attribute__((noinline));
static void handle_privileges(void)
{
    // try to increase priority (only root)
    setpriority(PRIO_PROCESS, 0, -20);

    if (drop_privileges() < 0)
    {
        fprintf(stderr, "Error dropping root privileges\n");
    }
}


static void audio_callback_proc(void *inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
    CWS_FillOutputBuffer(midi_queue_buffer[output_counter]->mAudioData, 4 * output_num_samples[output_counter], CWS_GetTime());

    if (reverb_effect)
    {
        CWS_ApplyReverb(midi_queue_buffer[output_counter]->mAudioData, 4 * output_num_samples[output_counter]);
    }

    midi_queue_buffer[output_counter]->mAudioDataByteSize = 4 * output_num_samples[output_counter];
    AudioQueueEnqueueBuffer(midi_pcm_queue, midi_queue_buffer[output_counter], 0, NULL);
    output_counter++;
    if (output_counter >= 100) output_counter = 0;
}

static int open_pcm_output(void) __attribute__((noinline));
static int open_pcm_output(void)
{
    AudioStreamBasicDescription format;
    OSStatus err;

    format.mSampleRate = frequency;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = 4;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 4;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;
    format.mReserved = 0;

    err = AudioQueueNewOutput(&format, audio_callback_proc, NULL, NULL, NULL, 0, &midi_pcm_queue);
    if (err != noErr)
    {
        fprintf(stderr, "Error creating PCM queue: %i\n", err);
        return -1;
    }

    for (int i = 0; i < 100; i++)
    {
        err = AudioQueueAllocateBuffer(midi_pcm_queue, 4 * output_num_samples[i], &midi_queue_buffer[i]);
        if (err != noErr)
        {
            fprintf(stderr, "Error allocating queue buffer: %i\n", err);
            return -2;
        }

        midi_queue_buffer[i]->mUserData = (void *)(uintptr_t)i;
    }

    return 0;
}

static void close_pcm_output(void) __attribute__((noinline));
static void close_pcm_output(void)
{
    AudioQueueDispose(midi_pcm_queue, 1);
}


static int open_midi_endpoint(void) __attribute__((noinline));
static int open_midi_endpoint(void)
{
    OSStatus err;

    err = MIDIClientCreate(CFSTR(MIDI_NAME), NULL, NULL, &midi_client);
    if (err != noErr)
    {
        fprintf(stderr, "Error creating MIDI client: %i\n", err);
        return -1;
    }

#if (MIDI_API >= 0)
#if (MIDI_API == 0)
    if (__builtin_available(macOS 11.0, *))
#endif
    {
        err = MIDIDestinationCreateWithProtocol(midi_client, CFSTR(MIDI_NAME), kMIDIProtocol_1_0, &midi_endpoint, ^(const MIDIEventList *evtlist, void * __nullable srcConnRefCon) { midi_receive_proc(evtlist, srcConnRefCon); } );
    }
#endif
#if (MIDI_API <= 0)
#if (MIDI_API == 0)
    else
#endif
    {
        err = MIDIDestinationCreate(midi_client, CFSTR(MIDI_NAME), midi_read_proc, NULL, &midi_endpoint);
    }
#endif
    if (err != noErr)
    {
        MIDIClientDispose(midi_client);
        fprintf(stderr, "Error creating MIDI destination: %i\n", err);
        return -2;
    }

    printf("MIDI destination is %s\n", MIDI_NAME);

    if (MIDIObjectSetIntegerProperty(midi_endpoint, kMIDIPropertyUniqueID, midi_name_crc32) == noErr)
    {
        printf("Unique ID is %i\n", (int)(int32_t)midi_name_crc32);
    }
    else
    {
        int32_t unique_id;

        for (int i = 0; i < 32; i++)
        {
            unique_id = midi_name_crc32 ^ (1 << i);
            err = MIDIObjectSetIntegerProperty(midi_endpoint, kMIDIPropertyUniqueID, unique_id);
            if (err == noErr)
            {
                printf("Unique ID is %i\n", (int)unique_id);
                break;
            }
        }

        if (err != noErr)
        {
            if (MIDIObjectGetIntegerProperty(midi_endpoint, kMIDIPropertyUniqueID, &unique_id) == noErr)
            {
                printf("Unique ID is %i\n", (int)unique_id);
            }
        }
    }

    return 0;
}

static void close_midi_endpoint(void) __attribute__((noinline));
static void close_midi_endpoint(void)
{
    MIDIEndpointDispose(midi_endpoint);
    MIDIClientDispose(midi_client);
}


static void main_loop(void) __attribute__((noinline));
static void main_loop(void)
{
    int is_paused;
    uint64_t last_written_time, current_time;

    for (int i = 2; i < 100; i++)
    {
        midi_queue_buffer[i]->mAudioDataByteSize = 4 * output_num_samples[i];
        memset(midi_queue_buffer[i]->mAudioData, 0, 4 * output_num_samples[i]);
        AudioQueueEnqueueBuffer(midi_pcm_queue, midi_queue_buffer[i], 0, NULL);
    }

    // pause pcm playback at the beginning
    is_paused = 1;

    midi_event_written = 0;

    for (;;)
    {
        if (is_paused)
        {
            struct timespec req;

            req.tv_sec = 0;
            req.tv_nsec = 10000000;
            nanosleep(&req, NULL);

            if (midi_event_written)
            {
                midi_event_written = 0;

                // remember time of last written event
                last_written_time = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);

                if (AudioQueueStart(midi_pcm_queue, NULL) == noErr)
                {
                    is_paused = 0;
                    printf("PCM playback unpaused\n");
                }
            }
            else
            {
                continue;
            }
        }

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1, 0);

        current_time = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (midi_event_written)
        {
            midi_event_written = 0;

            // remember time of last written event
            last_written_time = current_time;

            continue;
        }

        // if more than 60 seconds elapsed from last written event, then pause pcm playback
        if (current_time - last_written_time > 60000000000ull)
        {
            if (AudioQueuePause(midi_pcm_queue) == noErr)
            {
                is_paused = 1;
                printf("PCM playback paused\n");
            }
            else
            {
                // if pausing doesn't work then set time of last written event as current time, so the next attempt to pause will be in 60 seconds
                last_written_time = current_time;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    read_arguments(argc, argv);

    if (daemonize)
    {
        if (run_as_daemon_start(argv) < 0)
        {
            return 1;
        }
    }

    if (start_synth() < 0)
    {
        return 2;
    }

    handle_privileges();

    if (open_pcm_output() < 0)
    {
        stop_synth();
        return 5;
    }

    if (open_midi_endpoint() < 0)
    {
        close_pcm_output();
        stop_synth();
        return 6;
    }

    if (daemonize)
    {
        run_as_daemon_finish();
    }

    main_loop();

    close_midi_endpoint();
    close_pcm_output();
    stop_synth();
    return 0;
}