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

#include "cwssynth.h"

#if defined(__ARM_NEON__) || defined(__ARM_NEON) || defined(_M_ARM64) || defined(_M_ARM64EC)
#include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <immintrin.h>
#elif ( \
    (defined(__ARM_ARCH) && (__ARM_ARCH >= 6)) || \
    (defined(_M_ARM) && (_M_ARM >= 6)) || \
    (defined(__TARGET_ARCH_ARM) && (__TARGET_ARCH_ARM >= 6)) || \
    (defined(__TARGET_ARCH_THUMB) && (__TARGET_ARCH_THUMB >= 3)) \
)
#define ARMV6 1
#if defined(__ARM_ACLE) && __ARM_FEATURE_SAT
#include <arm_acle.h>
#endif
#endif


static const float preset1revgain[4] = { 0.596f, 0.442f, 0.327f, 0.154f };
static const float preset1revdel[4] = { 0.023f, 0.017f, 0.012f, 0.060f };
static const float preset1revfc = 0.780f;
static const float preset1revfwd = 0.600f;

static const float preset2revgain[4] = { 0.481f, 0.154f, 0.115f, 0.115f };
static const float preset2revdel[4] = { 0.127f, 0.058f, 0.023f, 0.040f };
static const float preset2revfc = 0.692f;
static const float preset2revfwd = 0.885f;

static float presetrevgain[4] = { 0.900f, 0.900f, 0.300f, 0.300f }; // coefficient for first/second/third/fourth tap delay
static float presetrevdel[4] = { 0.0113f, 0.0127f, 0.0372f, 0.0443f }; // delay of first/second/third/fourth tap in ms (milliseconds) - that's written in readme.txt, but the value is in seconds
static float presetrevfc = 0.780f; // low pass filter coefficient
static float presetrevfwd = 0.500f; // mix level coefficient of reverb effect

static uint32_t presetrevdelsamples[4]; // delay of first/second/third/fourth tap in number of samples

static uint32_t reverb_index[4] = { 0, 0, 0, 0 };
static float reverb_basevalue[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float reverb_buffer[4 * 12000];


static void CWS_SetReverbFrequency(uint32_t frequency)
{
    int index;

    for (index = 0; index < 4; index++)
    {
        presetrevdelsamples[index] = (int32_t)(presetrevdel[index] * (int32_t)frequency);
    }
}

void CCALL CWS_SetReverbPreset(int32_t preset, uint32_t frequency)
{
    if (preset == 0)
    {
        presetrevgain[0] = preset1revgain[0];
        presetrevgain[1] = preset1revgain[1];
        presetrevgain[2] = preset1revgain[2];
        presetrevgain[3] = preset1revgain[3];

        presetrevdel[0] = preset1revdel[0];
        presetrevdel[1] = preset1revdel[1];
        presetrevdel[2] = preset1revdel[2];
        presetrevdel[3] = preset1revdel[3];

        presetrevfc = preset1revfc;
        presetrevfwd = preset1revfwd;
    }
    else if (preset == 1)
    {
        presetrevgain[0] = preset2revgain[0];
        presetrevgain[1] = preset2revgain[1];
        presetrevgain[2] = preset2revgain[2];
        presetrevgain[3] = preset2revgain[3];

        presetrevdel[0] = preset2revdel[0];
        presetrevdel[1] = preset2revdel[1];
        presetrevdel[2] = preset2revdel[2];
        presetrevdel[3] = preset2revdel[3];

        presetrevfc = preset2revfc;
        presetrevfwd = preset2revfwd;
    }

    CWS_SetReverbFrequency(frequency);
}

void CCALL CWS_SetReverbCustom(float revgain0, float revgain1, float revgain2, float revgain3, float revdel0, float revdel1, float revdel2, float revdel3, float revfc, float revfwd, uint32_t frequency)
{
    presetrevgain[0] = revgain0;
    presetrevgain[1] = revgain1;
    presetrevgain[2] = revgain2;
    presetrevgain[3] = revgain3;

    presetrevdel[0] = revdel0;
    presetrevdel[1] = revdel1;
    presetrevdel[2] = revdel2;
    presetrevdel[3] = revdel3;

    presetrevfc = revfc;
    presetrevfwd = revfwd;

    CWS_SetReverbFrequency(frequency);
}

static void CWS_CalcReverb(uint32_t num, float value, float *presult)
{
    reverb_basevalue[num] = (1.0f - presetrevfc) * value + reverb_basevalue[num] * presetrevfc;
    reverb_buffer[num * 12000u + reverb_index[num]] = reverb_basevalue[num];
    // WARNING: the buffer only stores last 12000 values, so if presetrevdelsamples[num] is 12000 or higher, then wrong values get read from the buffer
    // presetrevdelsamples values are below 12000 for standard presets, but might get higher in a custom preset
    *presult = reverb_basevalue[num] * presetrevgain[num] + reverb_buffer[num * 12000u + (((uint32_t)((reverb_index[num] - presetrevdelsamples[num]) + 12000u)) % 12000u)];
    reverb_index[num] = (reverb_index[num] + 1) % 12000u;
}

void CCALL CWS_ApplyReverb(int16_t *buffer, uint32_t length_bytes)
{
    uint32_t remaining_samples;
    float fvalue1, fvalue2;
    int32_t ivalue[2];

    for (remaining_samples = (length_bytes + 3) >> 2; remaining_samples != 0; remaining_samples--, buffer += 2)
    {
        CWS_CalcReverb(0, (float)((buffer[0] + buffer[1]) / 2), &fvalue1);
        CWS_CalcReverb(1, fvalue1, &fvalue2);
        CWS_CalcReverb(2, fvalue2, &fvalue1);
        CWS_CalcReverb(3, fvalue1, &fvalue2);

        ivalue[0] = (int32_t)(fvalue2 * presetrevfwd + buffer[0]);
        ivalue[1] = (int32_t)(fvalue2 * presetrevfwd + buffer[1]);

#if defined(__ARM_NEON__) || defined(__ARM_NEON) || defined(_M_ARM64) || defined(_M_ARM64EC)
        {
            int32x2_t d1;
            int32x4_t q2;
            int16x4_t d3;

            d1 = vld1_s32(ivalue);
            q2 = vcombine_s32(d1, d1);
            d3 = vqmovn_s32(q2);
            vst1_lane_s32((int32_t *)buffer, vreinterpret_s32_s16(d3), 0);
        }
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        {
            __m128i xmm0;

            xmm0 =_mm_loadu_si64(ivalue);
            xmm0 = _mm_packs_epi32(xmm0, xmm0);
            *(int32_t *)buffer = _mm_cvtsi128_si32(xmm0);
        }
#else
#ifdef ARMV6
#if defined(__ARM_ACLE) && __ARM_FEATURE_SAT
        ivalue[0] = __ssat(ivalue[0], 16);
#else
        asm ( "ssat %[result], #16, %[value]" : [result] "=r" (ivalue[0]) : [value] "r" (ivalue[0]) : "cc" );
#endif
#else
        if (ivalue[0] > 32767) ivalue[0] = 32767;
        else if (ivalue[0] < -32768) ivalue[0] = -32768;
#endif
        buffer[0] = ivalue[0];

#ifdef ARMV6
#if defined(__ARM_ACLE) && __ARM_FEATURE_SAT
        ivalue[1] = __ssat(ivalue[1], 16);
#else
        asm ( "ssat %[result], #16, %[value]" : [result] "=r" (ivalue[1]) : [value] "r" (ivalue[1]) : "cc" );
#endif
#else
        if (ivalue[1] > 32767) ivalue[1] = 32767;
        else if (ivalue[1] < -32768) ivalue[1] = -32768;
#endif
        buffer[1] = ivalue[1];
#endif
    }
}

