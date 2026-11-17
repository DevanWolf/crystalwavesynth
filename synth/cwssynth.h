/**
 *
 *  Copyright (C) 2026 Roman Pauer
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

#if !defined(_CWSSYNTH_H_INCLUDED_)
#define _CWSSYNTH_H_INCLUDED_

#include <stdint.h>

#if defined(__GNUC__) && (defined(__i386) || (defined(__x86_64) && defined(_WIN32)))
    #define CCALL __attribute__ ((__cdecl__))
#elif defined(_MSC_VER)
    #define CCALL __cdecl
#else
    #define CCALL
#endif


#pragma pack(2)
typedef struct {
    uint16_t time_low;
    uint16_t time_high;
    uint16_t type;
    uint16_t channel;
    uint16_t value1;
    uint16_t value2;
    uint16_t value3;
} event_t;

typedef struct {
    uint32_t size;
    volatile uint16_t read_offset;
    volatile uint16_t write_offset;
    uint8_t buffer[240 * sizeof(event_t)];
} event_buffer_t;
#pragma pack()


#ifdef __cplusplus
extern "C" {
#endif

extern void CCALL CWS_MidiMessageShort(uint32_t time, uint32_t message);

extern void CCALL CWS_InitializeSynth(const uint8_t *sample_set, event_buffer_t *event_buffer, uint32_t chorus_on, uint32_t chorus_depth, uint32_t chorus_rate, uint32_t global_gain, uint32_t voice_cap, uint32_t output_frequency, uint32_t enhanced_mode);
extern void CCALL CWS_FillOutputBuffer(int16_t *output_buffer, uint32_t length_bytes, uint32_t current_time);

extern void CCALL CWS_SetReverbPreset(int32_t preset, uint32_t frequency);
extern void CCALL CWS_SetReverbCustom(float revgain0, float revgain1, float revgain2, float revgain3, float revdel0, float revdel1, float revdel2, float revdel3, float revfc, float revfwd, uint32_t frequency);
extern void CCALL CWS_ApplyReverb(int16_t *buffer, uint32_t length_bytes);

#ifdef __cplusplus
}
#endif

#endif

