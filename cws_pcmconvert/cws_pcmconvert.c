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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "midi_loader.h"

#include "cwssynth.h"

#if (defined(__WIN32__) || defined(__WINDOWS__)) && !defined(_WIN32)
#define _WIN32
#endif

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
#else
    #include <sys/types.h>
    #include <dirent.h>
#endif

#if defined(_MSC_VER)

#undef BIG_ENDIAN_BYTE_ORDER

#elif defined(__BYTE_ORDER__)

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define BIG_ENDIAN_BYTE_ORDER
#else
#undef BIG_ENDIAN_BYTE_ORDER
#endif

#else

#include <endian.h>
#if (__BYTE_ORDER == __BIG_ENDIAN)
#define BIG_ENDIAN_BYTE_ORDER
#else
#undef BIG_ENDIAN_BYTE_ORDER
#endif

#endif

#if defined(__GNUC__)
#define INLINE __inline__
#elif defined(_MSC_VER)
#define INLINE __inline
#else
#define INLINE inline
#endif


static const char *arg_input = NULL;
static const char *arg_output = NULL;
static const char *arg_data = "cwcwtsmp.dat";
static int wav_to_file = 1;

static void *datafile_ptr;
static uint32_t current_time;

static unsigned int timediv;
static midi_event_info *midi_events;

static int frequency, polyphony, volume, chorus_effect, reverb_effect, enhanced_mode;
static int chorus_depth, chorus_rate;
static float reverb_gain[4], reverb_del[4], reverb_fc, reverb_fwd;

static event_buffer_t event_buffer;

static int16_t output_buffer[480*2];
static unsigned int bytes_per_call, samples_per_call;

#ifdef __cplusplus
extern "C" {
#endif

event_buffer_t *cws_event_buffer;

#ifdef __cplusplus
}
#endif


static INLINE void WRITE_LE_UINT16(uint8_t *ptr, uint16_t value)
{
    ptr[0] = value & 0xff;
    ptr[1] = (value >> 8) & 0xff;
}

static INLINE void WRITE_LE_UINT32(uint8_t *ptr, uint32_t value)
{
    ptr[0] = value & 0xff;
    ptr[1] = (value >> 8) & 0xff;
    ptr[2] = (value >> 16) & 0xff;
    ptr[3] = (value >> 24) & 0xff;
}


static uint8_t *load_data_file(const char *datapath)
{
    FILE *f;
    uint8_t *mem;
    long datalen;

#if (defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__) || (defined(__MINGW32__) && defined(_UCRT)) || (defined(__STDC_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__)
    if (fopen_s(&f, datapath, "rb"))
#else
    f = fopen(datapath, "rb");
#endif
    if (f == NULL)
    {
#ifndef _WIN32
        char *pathcopy, *slash, *filename;
        DIR *dir;
        struct dirent *entry;

        pathcopy = strdup(datapath);
        if (pathcopy == NULL) return NULL;

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
            return NULL;
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
            return NULL;
        }

#if (defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__) || (defined(__MINGW32__) && defined(_UCRT)) || (defined(__STDC_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__)
        if (fopen_s(&f, pathcopy, "rb"))
#else
        f = fopen(pathcopy, "rb");
        if (f == NULL)
#endif
        {
            free(pathcopy);
            return NULL;
        }

        free(pathcopy);
#else
        return NULL;
#endif
    }

    if (fseek(f, 0, SEEK_END))
    {
        fclose(f);
        return NULL;
    }

    datalen = ftell(f);
    if (datalen <= 128)
    {
        fclose(f);
        return NULL;
    }

    if (fseek(f, 0, SEEK_SET))
    {
        fclose(f);
        return NULL;
    }

    mem = (uint8_t *)malloc(datalen);
    if (mem == NULL)
    {
        fclose(f);
        return NULL;
    }

    if (fread(mem, 1, datalen, f) != (unsigned long)datalen)
    {
        free(mem);
        fclose(f);
        return NULL;
    }

    return mem;
}


static void usage(const char *progname)
{
    static const char basename[] = "cws_pcmconvert";

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

#ifdef _WIN32
        slash = strrchr(progname, '\\');
        if (slash != NULL)
        {
            progname = slash + 1;
        }
#endif
    }

    printf(
        "%s - CrystalWave Software Synthesizer pcm convert\n"
        "Usage: %s [OPTIONS]...\n"
        "  -i PATH  Input path (path to .mid)\n"
        "  -s       Output raw data do stdout\n"
        "  -o PATH  Output path (path to .wav)\n"
        "  -w PATH  Datafile path (path to cwcwtsmp.dat)\n"
        "  -f NUM   Frequency (8000-48000 Hz [default=22050])\n"
        "  -p NUM   Polyphony (8-128 [default=32])\n"
        "  -v NUM   Volume (0-1023 [default=752])\n"
        "  -c NUM   Chorus effect (0=off [default], 1=on)\n"
        "  -r NUM   Reverb effect (0=off [default], 1=small room, 2=large room, 3=custom)\n"
        "  -m NUM   Mode (0 = original, 1 = enhanced [default])\n"
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

int main(int argc, char *argv[])
{
    int return_value;

    // basic settings
    frequency = 22050;
    polyphony = 32;
    volume = 752;
    chorus_effect = 0;
    reverb_effect = 0;
    enhanced_mode = 1;

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

    // parse arguments
    if (argc > 1)
    {
        int i, j;
        double k;
        for (i = 1; i < argc; i++)
        {
            if (argv[i][0] == '-' && argv[i][1] != 0 && argv[i][2] == 0)
            {
                switch (argv[i][1])
                {
                    case 'i': // input
                        if ((i + 1) < argc)
                        {
                            i++;
                            arg_input = argv[i];
                        }
                        break;
                    case 'o': // output
                        if ((i + 1) < argc)
                        {
                            i++;
                            arg_output = argv[i];
                        }
                        break;
                    case 'w': // data file
                        if ((i + 1) < argc)
                        {
                            i++;
                            arg_data = argv[i];
                        }
                        break;
                    case 's': // stdout
                        wav_to_file = 0;
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

    if (arg_input == NULL)
    {
        fprintf(stderr, "no input file\n");
        usage(argv[0]);
    }
    if (wav_to_file && arg_output == NULL)
    {
        fprintf(stderr, "no output file\n");
        usage(argv[0]);
    }

    // load DATA file
    datafile_ptr = load_data_file(arg_data);
    if (datafile_ptr == NULL)
    {
        fprintf(stderr, "error loading DATA file\n");
        return 3;
    }

    // load MIDI file
    if (load_midi_file(arg_input, &timediv, &midi_events))
    {
        free(datafile_ptr);
        fprintf(stderr, "error loading MIDI file\n");
        return 4;
    }


    return_value = 0;


    current_time = 0;

    samples_per_call = frequency / 100;
    bytes_per_call = 4 * samples_per_call;

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


    // play midi
    {
        unsigned int num_calls, remaining_events;
        midi_event_info *cur_event;

        FILE *fout;

        if (wav_to_file)
        {
            uint8_t wav_header[44];
            uint8_t *header_ptr;

#if (defined(_MSC_VER) && __STDC_WANT_SECURE_LIB__) || (defined(__MINGW32__) && defined(_UCRT)) || (defined(__STDC_LIB_EXT1__) && __STDC_WANT_LIB_EXT1__)
            if (fopen_s(&fout, arg_output, "wb"))
#else
            fout = fopen(arg_output, "wb");
            if (fout == NULL)
#endif
            {
                free_midi_data(midi_events);
                free(datafile_ptr);
                fprintf(stderr, "error opening output file\n");
                return 5;
            }

            // wav header
            header_ptr = wav_header;
            WRITE_LE_UINT32(header_ptr, 0x46464952);        // "RIFF" tag
            WRITE_LE_UINT32(header_ptr + 4, 36);            // RIFF length - filled later
            WRITE_LE_UINT32(header_ptr + 8, 0x45564157);    // "WAVE" tag
            header_ptr += 12;

            // fmt chunk
            WRITE_LE_UINT32(header_ptr, 0x20746D66);    // "fmt " tag
            WRITE_LE_UINT32(header_ptr + 4, 16);        // chunk length
            header_ptr += 8;

            // PCMWAVEFORMAT structure
            WRITE_LE_UINT16(header_ptr, 1);                     // wFormatTag - 1 = PCM
            WRITE_LE_UINT16(header_ptr + 2, 2);                 // nChannels - 2 = stereo
            WRITE_LE_UINT32(header_ptr + 4, frequency);         // nSamplesPerSec
            WRITE_LE_UINT32(header_ptr + 8, 4 * frequency);     // nAvgBytesPerSec
            WRITE_LE_UINT16(header_ptr + 12, 4);                // nBlockAlign
            WRITE_LE_UINT16(header_ptr + 14, 16);               // wBitsPerSample
            header_ptr += 16;

            // data chunk
            WRITE_LE_UINT32(header_ptr, 0x61746164);    // "data" tag
            WRITE_LE_UINT32(header_ptr + 4, 0);         // chunk length - filled later
            header_ptr += 8;

            if (fwrite(wav_header, 1, 44, fout) != 44)
            {
                fprintf(stderr, "error writing to output file\n");
                return 6;
            }
        }
        else
        {
#ifdef _WIN32
            if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            {
                fprintf(stderr, "error changing output mode to binary\n");
                return 6;
            }
#endif
            fout = stdout;
        }

        num_calls = 0;
        remaining_events = midi_events[0].len;
        cur_event = midi_events + 1;
        while (current_time < midi_events[0].time + 112)
        {
            uint32_t next_time;

            num_calls++;
            next_time = ((num_calls * samples_per_call + (samples_per_call >> 1)) * (uint64_t)1000) / frequency;
            while ((remaining_events > 0) && (cur_event->time <= next_time))
            {
                if (cur_event->len <= 3)
                {
                    if (cur_event->data[0] != 0xff) // skip meta events
                    {
                        CWS_MidiMessageShort(cur_event->time, cur_event->data[0] | (cur_event->data[1] << 8) | (cur_event->data[2] << 16));
                    }
                }

                cur_event++;
                remaining_events--;
            }

            CWS_FillOutputBuffer(output_buffer, bytes_per_call, current_time + 35);

            current_time = next_time;

            if (reverb_effect)
            {
                CWS_ApplyReverb(output_buffer, bytes_per_call);
            }

#ifdef BIG_ENDIAN_BYTE_ORDER
            // swap values to little-endian
            {
                int i;
                for (i = 0; i < bytes_per_call; i += 2)
                {
                    uint8_t value;
                    value = ((uint8_t *)output_buffer)[i];
                    ((uint8_t *)output_buffer)[i] = ((uint8_t *)output_buffer)[i + 1];
                    ((uint8_t *)output_buffer)[i + 1] = value;
                }
            }
#endif

            if (fwrite(output_buffer, 1, bytes_per_call, fout) != bytes_per_call)
            {
                fprintf(stderr, "error writing to output file\n");
                return_value = 6;
                break;
            }
        }

        if (wav_to_file)
        {
            uint8_t chunk_length[4];

            // RIFF length
            WRITE_LE_UINT32(chunk_length, 36 + num_calls * bytes_per_call);
            fseek(fout, 4, SEEK_SET);
            if (fwrite(chunk_length, 4, 1, fout) != 1)
            {
                fprintf(stderr, "error writing to output file\n");
                return 6;
            }

            // data chunk length
            WRITE_LE_UINT32(chunk_length, num_calls * bytes_per_call);
            fseek(fout, 40, SEEK_SET);
            if (fwrite(chunk_length, 4, 1, fout) != 1)
            {
                fprintf(stderr, "error writing to output file\n");
                return 6;
            }

            fclose(fout);
        }
        else
        {
            fflush(fout);
            fout = NULL;
        }
    }


    // free MIDI file, DATA file
    free_midi_data(midi_events);
    free(datafile_ptr);

    return return_value;
}

