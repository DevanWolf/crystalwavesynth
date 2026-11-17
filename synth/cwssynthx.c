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
#include <stddef.h>
#include <math.h>

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

#if defined(__GNUC__)
#define INLINE __inline__
#elif defined(_MSC_VER)
#define INLINE __inline
#else
#define INLINE inline
#endif


typedef struct _voice_t {
    struct _voice_t *next;
    uint32_t reserved;
    uint32_t field_08;
    uint32_t (*envelope_func)(struct _voice_t *, struct _voice_t **);
    float note_volume;
    float volume_right;
    float volume_left;
    uint32_t sample_pos;
    uint32_t field_20;
    uint32_t field_24;
    uint32_t field_28;
    const int8_t *sample_start;
    const int8_t *loop_start;
    const int8_t *sample_end;
    const int8_t *field_38;
    const int8_t *sample_ptr;
    uint32_t loop_length;
    uint32_t field_44[16];
    uint16_t field_84;
    uint16_t field_86;
    int16_t current_value;
    int16_t next_value;
    uint16_t field_8C;
    uint16_t field_8E;
    uint16_t field_90;
    uint16_t field_92;
    int16_t field_94;
    uint8_t note_num; // on drum channel: note_num - 25
    uint8_t channel_num;
    uint8_t field_98;
    uint8_t sustained_note;
    uint8_t nolooping;
    uint8_t field_9B;
} voice_t;


// the formula for this table is: frequency_table[x] = trunc(pow(2.0, (x - 127) / 1536.0) * 16384.0)
// except the last value: frequency_table[255] = trunc(pow(2.0, ((23110.0 / 9.0) - 127.0) / 1536.0) * 16384.0)
static const uint16_t frequency_table[256] =
{
    15471, 15478, 15485, 15492, 15499, 15506, 15513, 15520,
    15527, 15534, 15541, 15548, 15555, 15562, 15569, 15576,
    15583, 15590, 15597, 15604, 15611, 15618, 15625, 15632,
    15639, 15646, 15653, 15661, 15668, 15675, 15682, 15689,
    15696, 15703, 15710, 15717, 15724, 15731, 15738, 15746,
    15753, 15760, 15767, 15774, 15781, 15788, 15795, 15803,
    15810, 15817, 15824, 15831, 15838, 15845, 15853, 15860,
    15867, 15874, 15881, 15888, 15896, 15903, 15910, 15917,
    15924, 15931, 15939, 15946, 15953, 15960, 15967, 15975,
    15982, 15989, 15996, 16004, 16011, 16018, 16025, 16032,
    16040, 16047, 16054, 16061, 16069, 16076, 16083, 16090,
    16098, 16105, 16112, 16119, 16127, 16134, 16141, 16149,
    16156, 16163, 16170, 16178, 16185, 16192, 16200, 16207,
    16214, 16222, 16229, 16236, 16244, 16251, 16258, 16266,
    16273, 16280, 16288, 16295, 16302, 16310, 16317, 16324,
    16332, 16339, 16347, 16354, 16361, 16369, 16376, 16384,
    16391, 16398, 16406, 16413, 16421, 16428, 16435, 16443,
    16450, 16458, 16465, 16472, 16480, 16487, 16495, 16502,
    16510, 16517, 16525, 16532, 16540, 16547, 16554, 16562,
    16569, 16577, 16584, 16592, 16599, 16607, 16614, 16622,
    16629, 16637, 16644, 16652, 16659, 16667, 16674, 16682,
    16689, 16697, 16705, 16712, 16720, 16727, 16735, 16742,
    16750, 16757, 16765, 16773, 16780, 16788, 16795, 16803,
    16810, 16818, 16826, 16833, 16841, 16848, 16856, 16864,
    16871, 16879, 16886, 16894, 16902, 16909, 16917, 16925,
    16932, 16940, 16948, 16955, 16963, 16970, 16978, 16986,
    16993, 17001, 17009, 17016, 17024, 17032, 17040, 17047,
    17055, 17063, 17070, 17078, 17086, 17093, 17101, 17109,
    17117, 17124, 17132, 17140, 17148, 17155, 17163, 17171,
    17179, 17186, 17194, 17202, 17210, 17217, 17225, 17233,
    17241, 17248, 17256, 17264, 17272, 17280, 17287, 17295,
    17303, 17311, 17319, 17326, 17334, 17342, 17350, 49291
};
// the formula for this table is: pan_table[x] = sqrtf(x / 127.0f)
static const float pan_table[128] =
{
    0.000000f, 0.088736f, 0.125491f, 0.153695f, 0.177471f, 0.198419f, 0.217357f, 0.234772f,
    0.250982f, 0.266207f, 0.280607f, 0.294303f, 0.307389f, 0.319941f, 0.332018f, 0.343672f,
    0.354943f, 0.365866f, 0.376473f, 0.386790f, 0.396838f, 0.406638f, 0.416207f, 0.425561f,
    0.434714f, 0.443678f, 0.452465f, 0.461084f, 0.469545f, 0.477856f, 0.486025f, 0.494059f,
    0.501965f, 0.509748f, 0.517413f, 0.524967f, 0.532414f, 0.539758f, 0.547003f, 0.554154f,
    0.561214f, 0.568185f, 0.575073f, 0.581879f, 0.588606f, 0.595257f, 0.601834f, 0.608341f,
    0.614779f, 0.621150f, 0.627456f, 0.633699f, 0.639882f, 0.646005f, 0.652071f, 0.658081f,
    0.664037f, 0.669939f, 0.675791f, 0.681591f, 0.687343f, 0.693048f, 0.698705f, 0.704317f,
    0.709885f, 0.715410f, 0.720892f, 0.726333f, 0.731733f, 0.737094f, 0.742416f, 0.747700f,
    0.752947f, 0.758158f, 0.763333f, 0.768473f, 0.773579f, 0.778652f, 0.783692f, 0.788700f,
    0.793676f, 0.798621f, 0.803536f, 0.808420f, 0.813276f, 0.818102f, 0.822901f, 0.827671f,
    0.832414f, 0.837130f, 0.841820f, 0.846484f, 0.851122f, 0.855736f, 0.860324f, 0.864888f,
    0.869428f, 0.873945f, 0.878438f, 0.882909f, 0.887357f, 0.891782f, 0.896186f, 0.900568f,
    0.904930f, 0.909270f, 0.913589f, 0.917889f, 0.922168f, 0.926427f, 0.930667f, 0.934888f,
    0.939090f, 0.943273f, 0.947437f, 0.951584f, 0.955712f, 0.959823f, 0.963916f, 0.967992f,
    0.972050f, 0.976092f, 0.980117f, 0.984126f, 0.988118f, 0.992095f, 0.996055f, 1.000000f
};
// the formula for this table is: volume_table[x] = powf(x - 127.0f, 2.0f)
static const float volume_table[128] =
{
    0.000000f, 0.000062f, 0.000248f, 0.000558f, 0.000992f, 0.001550f, 0.002232f, 0.003038f,
    0.003968f, 0.005022f, 0.006200f, 0.007502f, 0.008928f, 0.010478f, 0.012152f, 0.013950f,
    0.015872f, 0.017918f, 0.020088f, 0.022382f, 0.024800f, 0.027342f, 0.030008f, 0.032798f,
    0.035712f, 0.038750f, 0.041912f, 0.045198f, 0.048608f, 0.052142f, 0.055800f, 0.059582f,
    0.063488f, 0.067518f, 0.071672f, 0.075950f, 0.080352f, 0.084878f, 0.089528f, 0.094302f,
    0.099200f, 0.104222f, 0.109368f, 0.114638f, 0.120032f, 0.125550f, 0.131192f, 0.136958f,
    0.142848f, 0.148862f, 0.155000f, 0.161262f, 0.167648f, 0.174158f, 0.180792f, 0.187550f,
    0.194432f, 0.201438f, 0.208568f, 0.215822f, 0.223200f, 0.230702f, 0.238328f, 0.246078f,
    0.253953f, 0.261951f, 0.270073f, 0.278319f, 0.286689f, 0.295183f, 0.303801f, 0.312543f,
    0.321409f, 0.330399f, 0.339513f, 0.348751f, 0.358113f, 0.367599f, 0.377209f, 0.386943f,
    0.396801f, 0.406783f, 0.416889f, 0.427119f, 0.437473f, 0.447951f, 0.458553f, 0.469279f,
    0.480129f, 0.491103f, 0.502201f, 0.513423f, 0.524769f, 0.536239f, 0.547833f, 0.559551f,
    0.571393f, 0.583359f, 0.595449f, 0.607663f, 0.620001f, 0.632463f, 0.645049f, 0.657759f,
    0.670593f, 0.683551f, 0.696633f, 0.709839f, 0.723169f, 0.736623f, 0.750202f, 0.763903f,
    0.777730f, 0.791680f, 0.805754f, 0.819952f, 0.834274f, 0.848720f, 0.863290f, 0.877984f,
    0.892802f, 0.907744f, 0.922810f, 0.938000f, 0.953314f, 0.968752f, 0.984314f, 1.000000f
};
// the formula for this table is: exptabmod12plus[x] = trunc(pow(2.0, x / 12.0) * 65536.0)
static const uint32_t exptabmod12plus[12] = { 65536, 69432, 73561, 77935, 82569, 87480, 92681, 98193, 104031, 110217, 116771, 123715 };
// the formula for this table is: exptabmod12minus[x] = trunc(pow(2.0, x / -12.0) * 65536.0)
static const uint32_t exptabmod12minus[12] = { 65536, 61857, 58385, 55108, 52015, 49096, 46340, 43740, 41285, 38967, 36780, 34716 };


static event_buffer_t *event_buffer;
static uint32_t chorus_on;
static uint32_t chorus_depth;
static uint32_t chorus_rate;
static uint32_t global_gain;
static uint32_t voice_cap;
static uint32_t output_frequency;
static uint32_t enhanced_mode;

static const uint8_t *sample_set_data;

static uint32_t chorus_C0009034;
static float chorus_C000903C;
static float chorus_C0009050;
static void (*chorus_applyfunc)(float *value);
static float chorus_buffer[2048];
static uint32_t chorus_index1;
static uint32_t chorus_index2;

static voice_t voice_array[128];
static voice_t *first_active_voice;
static voice_t *first_inactive_voice;

static uint32_t output_frequency_counter;
static uint32_t output_frequency_div1000;


static float channel_volume[16] = { 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f };
static float channel_pan_left[16] = { 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f };
static float channel_pan_right[16] = { 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f, 0.707f };
static float channel_expression[16] = { 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f };
static uint8_t channel_sustain[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static int16_t channel_fine_tuning[16] = { 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000 };
static int16_t channel_coarse_tuning[16] = { 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000, 0x2000 };
static int16_t channel_pitch_bend[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static uint16_t channel_pitch_bend_sensitivity[16] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 };


static void ApplyChorusEnabled(float *value);
static void ApplyChorusDisabled(float *value);
static void CalculateVoiceFrequency(voice_t *voice);
static void CalculateVoiceVolume(voice_t *voice);


static INLINE uint32_t GetValue16(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0]) | (((uint32_t)ptr[1]) << 8);
}

static INLINE uint32_t GetValue24(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0]) | (((uint32_t)ptr[1]) << 8) | (((uint32_t)ptr[2]) << 16);
}

static INLINE uint32_t GetValue32(const uint8_t *ptr)
{
    return ((uint32_t)ptr[0]) | (((uint32_t)ptr[1]) << 8) | (((uint32_t)ptr[2]) << 16) | (((uint32_t)ptr[3]) << 24);
}

static void ApplyOptionsChorus(uint32_t _chorus_on, uint32_t _chorus_depth, uint32_t _chorus_rate)
{
    chorus_depth = _chorus_depth;
    chorus_C0009034 = (_chorus_depth * (uint64_t)output_frequency) / 10000u;
    chorus_rate = _chorus_rate;
    chorus_C0009050 = ((float)(int32_t)chorus_C0009034 * 4.0f) / ((((float)(int32_t)_chorus_rate * 0.045f) + 0.5f) * (float)(int32_t)output_frequency);
    if (_chorus_on)
    {
        chorus_applyfunc = ApplyChorusEnabled;
        chorus_on = 1;
    }
    else
    {
        chorus_applyfunc = ApplyChorusDisabled;
        chorus_on = 0;
    }
}

static void ApplyOptionsVoiceCap(uint32_t _voice_cap)
{
    first_active_voice = NULL;
    first_inactive_voice = &(voice_array[0]);
    if (_voice_cap > 128) _voice_cap = 128;
    voice_cap = _voice_cap;

    voice_array[_voice_cap - 1].next = NULL;
    for (_voice_cap--; _voice_cap != 0; _voice_cap--)
    {
        voice_array[_voice_cap - 1].next = &(voice_array[_voice_cap]);
    }
}

static void ApplyOptionsGlobalGain(uint32_t _global_gain)
{
    voice_t *current_active_voice;

    global_gain = _global_gain;

    for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
    {
        CalculateVoiceVolume(current_active_voice);
    }
}

static void ApplyOptionsOutFs(uint32_t _output_frequency)
{
    voice_t *current_active_voice;

    output_frequency = _output_frequency;
    output_frequency_div1000 = _output_frequency / 1000u;

    for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
    {
        CalculateVoiceFrequency(current_active_voice);
    }
}

static void ApplyOptionsAll(void)
{
    ApplyOptionsChorus(chorus_on, chorus_depth, chorus_rate);
    ApplyOptionsVoiceCap(voice_cap);
    ApplyOptionsGlobalGain(global_gain);
    ApplyOptionsOutFs(output_frequency);
}

void CCALL CWS_InitializeSynth(const uint8_t *_sample_set, event_buffer_t *_event_buffer, uint32_t _chorus_on, uint32_t _chorus_depth, uint32_t _chorus_rate, uint32_t _global_gain, uint32_t _voice_cap, uint32_t _output_frequency, uint32_t _enhanced_mode)
{
    _event_buffer->size = sizeof(event_buffer_t);
    _event_buffer->read_offset = 0;
    _event_buffer->write_offset = 0;
    event_buffer = _event_buffer;
    chorus_on = _chorus_on;
    chorus_depth = _chorus_depth;
    chorus_rate = _chorus_rate;
    global_gain = _global_gain;
    voice_cap = _voice_cap;
    output_frequency = _output_frequency;
    enhanced_mode = _enhanced_mode;

    chorus_C000903C = 0.0f;
    chorus_index1 = 661;
    chorus_index2 = 0;
    output_frequency_counter = 0;

    sample_set_data = _sample_set + 128;

    ApplyOptionsAll();
}

static voice_t *DeactivateVoice(voice_t *voice)
{
    voice_t *next_voice, *current_active_voice;

    next_voice = voice->next;
    voice->next = first_inactive_voice;
    first_inactive_voice = voice;

    if (first_active_voice == voice)
    {
        first_active_voice = next_voice;
    }
    else
    {
        current_active_voice = first_active_voice;
        while (current_active_voice->next != voice) current_active_voice = current_active_voice->next;

        current_active_voice->next = next_voice;
    }

    return next_voice;
}

static uint32_t EnvelopeFunc_C000BA4C(voice_t *voice, voice_t **voice_ptr)
{
    return 0;
}

static uint32_t EnvelopeFunc_C000BA60(voice_t *voice, voice_t **voice_ptr)
{
    uint32_t uval32;

    uval32 = (voice->field_92 * (uint64_t)voice->field_08) / 0x7FFFu;
    voice->field_08 = uval32;
    if (uval32 == 0)
    {
        voice->envelope_func = EnvelopeFunc_C000BA4C;
        *voice_ptr = DeactivateVoice(voice);
    }
    return uval32;
}

static uint32_t EnvelopeFunc_C000BA4F(voice_t *voice, voice_t **voice_ptr)
{
    voice->field_8E--;
    if (voice->field_8E == 0)
    {
        voice->envelope_func = EnvelopeFunc_C000BA60;
    }
    return 1; // WARNING: the original code didn't return a value, the return value was the return value from previous envelope function call
}

static uint32_t EnvelopeFunc_C000BA87(voice_t *voice, voice_t **voice_ptr)
{
    return voice->field_08;
}

static uint32_t EnvelopeFunc_C000BA8B(voice_t *voice, voice_t **voice_ptr)
{
    uint32_t uval32;

    uval32 = (voice->field_8E * (uint64_t)voice->field_08) / 0x7FFFu;
    if (uval32 <= voice->field_90)
    {
        voice->envelope_func = EnvelopeFunc_C000BA87;
        uval32 = voice->field_90;
    }
    voice->field_08 = uval32;
    if (uval32 == 0)
    {
        *voice_ptr = DeactivateVoice(voice);
    }
    return uval32;
}

static uint32_t EnvelopeFunc_C000BAC2(voice_t *voice, voice_t **voice_ptr)
{
    uint32_t uval32;

    uval32 = voice->field_8C + voice->field_08;
    if (uval32 >= 0x7FFF)
    {
        voice->envelope_func = EnvelopeFunc_C000BA8B;
        uval32 = 0x7FFF;
    }
    voice->field_08 = uval32;
    return uval32;
}

static void ProcessEnvelopes(void)
{
    voice_t *current_active_voice;

    current_active_voice = first_active_voice;
    while (current_active_voice != NULL)
    {
        if (!current_active_voice->envelope_func(current_active_voice, &current_active_voice)) continue;

        CalculateVoiceVolume(current_active_voice);

        current_active_voice = current_active_voice->next;
    }
}

static void CallFunctionOnChannelVoices(void (*func)(voice_t *), uint8_t channel)
{
    voice_t *current_active_voice;

    for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
    {
        if (current_active_voice->channel_num == channel)
        {
            func(current_active_voice);
        }
    }
}

static void ReleaseSustainedNote(voice_t *voice)
{
    if (voice->sustained_note)
    {
        voice->envelope_func = EnvelopeFunc_C000BA60;
        voice->sustained_note = 0;
    }
}

static void ResetAllControllers(uint8_t channel)
{
    channel_volume[channel] = 0.62f;
    channel_pan_left[channel] = 0.707f;
    channel_pan_right[channel] = 0.707f;
    channel_expression[channel] = 0.62f;
    channel_fine_tuning[channel] = 0x2000;
    channel_coarse_tuning[channel] = 0x2000;
    channel_sustain[channel] = 0;
    channel_pitch_bend_sensitivity[channel] = 2;
    channel_pitch_bend[channel] = 0;
    CallFunctionOnChannelVoices(CalculateVoiceVolume, channel);
    CallFunctionOnChannelVoices(CalculateVoiceFrequency, channel);
}

static void RPNChange(const event_t *event)
{
    switch(event->value1)
    {
        case 0: // Pitch bend sensitivity in semitones
            channel_pitch_bend_sensitivity[event->channel] = event->value2 >> 7;
            CallFunctionOnChannelVoices(CalculateVoiceFrequency, (uint8_t)event->channel);
            break;
        case 1: // Fine tuning in cents
            channel_fine_tuning[event->channel] = event->value2;
            CallFunctionOnChannelVoices(CalculateVoiceFrequency, (uint8_t)event->channel);
            break;
        case 2: // Coarse tuning in half-tones
            channel_coarse_tuning[event->channel] = event->value2;
            CallFunctionOnChannelVoices(CalculateVoiceFrequency, (uint8_t)event->channel);
            break;
        default:
            break;
    }
}

static void ControllerChange(const event_t *event)
{
    switch(event->value1)
    {
        case 7: // Volume
            channel_volume[event->channel] = volume_table[event->value2];
            CallFunctionOnChannelVoices(CalculateVoiceVolume, (uint8_t)event->channel);
            break;
        case 10: // Pan
            channel_pan_right[event->channel] = pan_table[event->value2];
            channel_pan_left[event->channel] = pan_table[127 - event->value2];
            CallFunctionOnChannelVoices(CalculateVoiceVolume, (uint8_t)event->channel);
            break;
        case 11: // Expression
            channel_expression[event->channel] = volume_table[event->value2];
            CallFunctionOnChannelVoices(CalculateVoiceVolume, (uint8_t)event->channel);
            break;
        case 64: // Sustain (damper) pedal
            if (event->value2 < 64)
            {
                channel_sustain[event->channel] = 0;
                CallFunctionOnChannelVoices(ReleaseSustainedNote, (uint8_t)event->channel);
            }
            else
            {
                channel_sustain[event->channel] = 1;
            }
            break;
        case 121: // Reset all controllers
            ResetAllControllers((uint8_t)event->channel);
            break;
        default:
            break;
    }
}

static void PitchBend(const event_t *event)
{
    channel_pitch_bend[event->channel] = event->value1 - 0x2000;
    CallFunctionOnChannelVoices(CalculateVoiceFrequency, (uint8_t)event->channel);
}

static void NoteOnPart(const event_t *event, voice_t *voice, const uint8_t *melody_data)
{
    unsigned int index;

    voice->note_volume = volume_table[event->value2];

    voice->field_8C = GetValue16(melody_data + 128);
    voice->field_8E = GetValue16(melody_data + 132);
    voice->field_90 = GetValue16(melody_data + 138);
    voice->field_92 = GetValue16(melody_data + 136);
    voice->field_94 = GetValue16(melody_data + 148);
    voice->envelope_func = EnvelopeFunc_C000BAC2;

    for (index = 0; index < 24; index += 4)
    {
        if (voice->note_num <= melody_data[index + 3]) break;
        if (voice->note_num <= melody_data[index + 2])
        {
            index++;
            break;
        }
        if (voice->note_num <= melody_data[index + 1])
        {
            index += 2;
            break;
        }
        if (voice->note_num <= melody_data[index])
        {
            index += 3;
            break;
        }
    }

    melody_data = sample_set_data + GetValue32(melody_data + index * 4u + 24);

    voice->field_28 = GetValue16(melody_data + 14);
    voice->field_84 = voice->note_num - voice->field_28;
    voice->field_24 = ((GetValue16(melody_data + 12) * (uint64_t)32000u) / output_frequency) << 1;

    CalculateVoiceFrequency(voice);

    voice->sample_pos = 0;
    voice->field_9B = 0;
    voice->sustained_note = 0;
    voice->sample_start = voice->sample_ptr = (const int8_t *)(melody_data + 36 + GetValue32(melody_data + 20));
    voice->loop_start = voice->sample_start + GetValue32(melody_data);
    voice->sample_end = voice->sample_start + GetValue32(melody_data + 4);
    voice->loop_length = GetValue32(melody_data + 4) - GetValue32(melody_data);
    voice->field_38 = voice->sample_start + GetValue32(melody_data + 8);
    for (index = 0; index < 16; index++)
    {
        voice->field_44[index] = 0;
    }
}

static void StealActiveVoice(void)
{
    voice_t *voice;

    voice = first_active_voice;
    // WARNING: original code steals oldest active voice (i.e. first in the list), but it also contains unused code that steals active voice with smallest value of field_08
    first_active_voice = voice->next;
    voice->next = first_inactive_voice;
    first_inactive_voice = voice;
}

static void ActivateVoice(voice_t *voice)
{
    voice_t *current_active_voice;

    voice->next = NULL;

    current_active_voice = first_active_voice;
    if (current_active_voice == NULL)
    {
        first_active_voice = voice;
    }
    else
    {
        while (current_active_voice->next != NULL)
        {
            current_active_voice = current_active_voice->next;
        }

        current_active_voice->next = voice;
    }
}

static voice_t *GetInactiveVoice(void)
{
    voice_t *voice;

    if (first_inactive_voice == NULL)
    {
        StealActiveVoice();
    }

    voice = first_inactive_voice;
    first_inactive_voice = voice->next;
    return voice;
}

static void RestoreDrum(uint16_t field_86)
{
    voice_t *current_active_voice;

    for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
    {
        if (current_active_voice->field_86 == field_86)
        {
            current_active_voice->envelope_func = EnvelopeFunc_C000BA60;
            current_active_voice->field_92 = 0x6000;
            break;
        }
    }
}

static void NoteOnDrumChannel(const event_t *event)
{
    voice_t *voice;
    const uint8_t *drum_data;
    unsigned int index;

    if (event->value1 < 25 || event->value1 >= 88) return;

    voice = GetInactiveVoice();
    voice->note_num = event->value1 - 25;
    voice->channel_num = 9;
    voice->field_98 = 1;
    voice->sustained_note = 0;
    voice->note_volume = volume_table[event->value2];
    voice->field_8C = 0x7FFF;
    voice->field_08 = 0x7FFF;

    drum_data = sample_set_data + GetValue24(sample_set_data + 128u * 4u + 1) + voice->note_num * 20u;

    voice->field_86 = GetValue16(drum_data + 4);
    if (voice->field_86 != 0)
    {
        RestoreDrum(voice->field_86);
    }
    voice->field_8E = GetValue16(drum_data + 14);
    voice->field_92 = GetValue16(drum_data + 12);
    voice->field_94 = GetValue16(drum_data + 6);
    voice->field_84 = GetValue16(drum_data + 10);
    voice->envelope_func = EnvelopeFunc_C000BA4F;

    CalculateVoiceVolume(voice);

    drum_data = sample_set_data + GetValue32(drum_data);

    voice->field_28 = GetValue16(drum_data + 14);
    voice->field_24 = ((GetValue16(drum_data + 12) * (uint64_t)32000u) / output_frequency) << 1;

    CalculateVoiceFrequency(voice);

    voice->sample_pos = 0;
    voice->field_9B = 0;
    voice->sample_start = voice->sample_ptr = (const int8_t *)(drum_data + 36 + GetValue32(drum_data + 20));
    voice->loop_start = voice->sample_start + GetValue32(drum_data);
    voice->sample_end = voice->sample_start + GetValue32(drum_data + 4);
    voice->loop_length = GetValue32(drum_data + 4) - GetValue32(drum_data);
    voice->nolooping = 0;
    if (voice->loop_length <= 80)
    {
        voice->nolooping = 1;
    }
    voice->field_38 = voice->sample_start + GetValue32(drum_data + 8);
    for (index = 0; index < 16; index++)
    {
        voice->field_44[index] = 0;
    }

    ActivateVoice(voice);
}

static void NoteOn(const event_t *event)
{
    voice_t *voice;
    const uint8_t *melody_data;
    uint8_t num_parts;

    if (event->channel == 9)
    {
        NoteOnDrumChannel(event);
        return;
    }

    melody_data = sample_set_data + event->value3 * 4u;
    num_parts = *melody_data;
    melody_data = sample_set_data + GetValue24(melody_data + 1);

    voice = GetInactiveVoice();
    voice->note_num = (uint8_t)event->value1;
    voice->channel_num = (uint8_t)event->channel;
    voice->field_98 = 1;
    voice->sustained_note = 0;
    voice->field_86 = 0;
    voice->nolooping = 0;

    NoteOnPart(event, voice, melody_data);
    CalculateVoiceVolume(voice);
    ActivateVoice(voice);

// WARNING: the original code only uses the first instrument part, but some instruments have two parts
    if (num_parts != 1 && enhanced_mode)
    {
        melody_data += 248;

        voice = GetInactiveVoice();
        voice->note_num = (uint8_t)event->value1;
        voice->channel_num = (uint8_t)event->channel;
        voice->field_98 = 1;
        voice->sustained_note = 0;
        voice->field_86 = 0;
        voice->nolooping = 0;

        NoteOnPart(event, voice, melody_data);
        CalculateVoiceVolume(voice);
        ActivateVoice(voice);
    }
}

static void NoteOff(const event_t *event)
{
    voice_t *current_active_voice;

    if (event->channel == 9) return;

    for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
    {
        if (event->value1 == current_active_voice->note_num && event->channel == current_active_voice->channel_num)
        {
            if (channel_sustain[event->channel])
            {
                current_active_voice->sustained_note = 1;
            }
            else
            {
                current_active_voice->envelope_func = EnvelopeFunc_C000BA60;
            }
        }
    }
}

static void ProcessEvent(const event_t *event)
{
    switch (event->type)
    {
        case 0:
            NoteOn(event);
            break;
        case 1:
            NoteOff(event);
            break;
        case 5:
            ControllerChange(event);
            break;
        case 6:
            PitchBend(event);
            break;
        case 7:
            RPNChange(event);
            break;
        default:
            break;
    }
}

static void ApplyChorusEnabled(float *value)
{
    float fvalue;
    int32_t sval32;

    fvalue = chorus_C000903C + chorus_C0009050;
    sval32 = (int32_t)fabsf(fvalue); // using function lrintf or roundf here would do the same rounding as the original code
    if ((uint32_t)sval32 >= chorus_C0009034)
    {
        chorus_C0009050 = -chorus_C0009050;
        fvalue += chorus_C0009050;
    }

    sval32 = (int32_t)fvalue; // using function lrintf or roundf here would do the same rounding as the original code
    chorus_C000903C = fvalue;

    value[0] *= 0.707f;
    chorus_buffer[chorus_index1] = value[0];

    value[1] *= 0.707f;
    chorus_buffer[chorus_index1 + 1024] = value[1];

    chorus_index1 = (chorus_index1 + 1) & 1023;

    sval32 += chorus_index2;
    if (sval32 < 0)
    {
        sval32 += 1024;
    }
    else if (sval32 >= 1024)
    {
        sval32 -= 1024;
    }

    value[1] += chorus_buffer[sval32 + 1024];
    value[0] += chorus_buffer[sval32];

    chorus_index2 = (chorus_index2 + 1) & 1023;
}

static void ApplyChorusDisabled(float *value)
{
    // do nothing
}

static void CalculateVoiceFrequency(voice_t *voice)
{
    uint32_t uval32;
    int16_t coarse_tune, fine_tune;

    uval32 = (int32_t)channel_pitch_bend[voice->channel_num];
    if ((int32_t)uval32 < 0) uval32 = -(int32_t)uval32;

    uval32 *= channel_pitch_bend_sensitivity[voice->channel_num];
    coarse_tune = uval32 >> 13;
    fine_tune = (uval32 >> 6) & 0x7f;
    if ((int32_t)channel_pitch_bend[voice->channel_num] < 0)
    {
        coarse_tune = -coarse_tune;
        fine_tune = -fine_tune;
    }

    coarse_tune += (channel_coarse_tuning[voice->channel_num] >> 7) - 0x40;
    fine_tune += (channel_fine_tuning[voice->channel_num] - 0x2000) >> 6; // WARNING: the original code uses unsigned shift, which is obviously wrong for negative numbers
    if (fine_tune < -127)
    {
        fine_tune += 128;
        coarse_tune--;
    }
    else if (fine_tune > 127)
    {
        fine_tune -= 128;
        coarse_tune++;
    }

    coarse_tune += voice->field_84;

    if (coarse_tune >= 0)
    {
        uval32 = exptabmod12plus[coarse_tune % 12] << (coarse_tune / 12);
    }
    else
    {
        uval32 = exptabmod12minus[(-coarse_tune) % 12] >> ((-coarse_tune) / 12);
    }

    uval32 = (voice->field_24 * (uint64_t)uval32) >> 15;
    voice->field_20 = (frequency_table[(uint16_t)(fine_tune + 127)] * (uint64_t)uval32) >> 14;
}

static void CalculateVoiceVolume(voice_t *voice)
{
    voice->volume_left = (((((((float)(int32_t)voice->field_08 * (float)voice->field_94) * voice->note_volume) * channel_volume[voice->channel_num]) * channel_expression[voice->channel_num]) * (float)(int32_t)global_gain) * 3.0e-12f) * channel_pan_left[voice->channel_num];
    voice->volume_right = (((((((float)(int32_t)voice->field_08 * (float)voice->field_94) * voice->note_volume) * channel_volume[voice->channel_num]) * channel_expression[voice->channel_num]) * (float)(int32_t)global_gain) * 3.0e-12f) * channel_pan_right[voice->channel_num];
}

void CCALL CWS_FillOutputBuffer(int16_t *output_buffer, uint32_t length_bytes, uint32_t current_time)
{
    uint32_t current_time_minus35, read_offset, event_time;
    event_t *event, current_event;
    voice_t *current_active_voice;
    float fvalue[2];

    current_time_minus35 = current_time - 35;

    for (length_bytes >>= 2; length_bytes != 0; length_bytes--)
    {
        if (output_frequency_counter == 0)
        {
            for (;;)
            {
                read_offset = event_buffer->read_offset;
                if (read_offset == event_buffer->write_offset) break;

                event = (event_t *)(event_buffer->buffer + read_offset);
                event_time = event->time_low | (event->time_high << 16);
                if (current_time_minus35 < event_time) break;

                current_event.type = event->type;
                current_event.channel = event->channel;
                current_event.value1 = event->value1;
                current_event.value2 = event->value2;
                current_event.value3 = event->value3;

                read_offset += sizeof(event_t);
                if (read_offset >= sizeof(event_buffer->buffer))
                {
                    read_offset = 0;
                }
                event_buffer->read_offset = read_offset;

                ProcessEvent(&current_event);
            }
        }

        fvalue[0] = fvalue[1] = 0.0f;
        for (current_active_voice = first_active_voice; current_active_voice != NULL; current_active_voice = current_active_voice->next)
        {
            for (current_active_voice->sample_pos += current_active_voice->field_20; current_active_voice->sample_pos >= 65536; current_active_voice->sample_pos -= 65536)
            {
                current_active_voice->current_value = current_active_voice->next_value;
                current_active_voice->next_value = *current_active_voice->sample_ptr << 8;
                current_active_voice->sample_ptr++;
                if ((uintptr_t)current_active_voice->sample_ptr >= (uintptr_t)current_active_voice->sample_end)
                {
                    if (current_active_voice->nolooping == 1)
                    {
                        current_active_voice->envelope_func = EnvelopeFunc_C000BA60;
                        current_active_voice->field_92 = 0;
                    }
                    current_active_voice->sample_ptr -= current_active_voice->loop_length;
                }
            }

            fvalue[0] += ((float)current_active_voice->current_value + (((float)current_active_voice->next_value - (float)current_active_voice->current_value) * (float)(int32_t)current_active_voice->sample_pos * (float)(1.0 / 65536.0))) * current_active_voice->volume_left;
            fvalue[1] += ((float)current_active_voice->current_value + (((float)current_active_voice->next_value - (float)current_active_voice->current_value) * (float)(int32_t)current_active_voice->sample_pos * (float)(1.0 / 65536.0))) * current_active_voice->volume_right;
        }

        chorus_applyfunc(fvalue);

#if defined(__ARM_NEON__) || defined(__ARM_NEON) || defined(_M_ARM64) || defined(_M_ARM64EC)
        {
            float32x2_t d0;
            int32x2_t d1;
            int32x4_t q2;
            int16x4_t d3;

            d0 = vld1_f32(fvalue);
            d1 = vcvt_s32_f32(d0);
            q2 = vcombine_s32(d1, d1);
            d3 = vqmovn_s32(q2);
            vst1_lane_s32((int32_t *)output_buffer, vreinterpret_s32_s16(d3), 0);
        }
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
        {
            __m128i xmm0;

            xmm0 =_mm_loadu_si64(fvalue);
            xmm0 = _mm_cvttps_epi32(_mm_castsi128_ps(xmm0));
            xmm0 = _mm_packs_epi32(xmm0, xmm0);
            *(int32_t *)output_buffer = _mm_cvtsi128_si32(xmm0);
        }
#else
        {
            int32_t ivalue[2];

            ivalue[0] = (int32_t)fvalue[0]; // using function lrintf or roundf here would do the same rounding as the original code
            ivalue[1] = (int32_t)fvalue[1]; // using function lrintf or roundf here would do the same rounding as the original code

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

            output_buffer[0] = ivalue[0];

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

            output_buffer[1] = ivalue[1];
        }
#endif

        output_buffer += 2;

        output_frequency_counter++;
        if (output_frequency_counter >= output_frequency_div1000)
        {
            output_frequency_counter = 0;
            current_time_minus35++;
            if (!(current_time_minus35 & 7))
            {
                ProcessEnvelopes();
            }
        }
    }
}

