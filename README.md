# CrystalWave Synthesizer

CrystalWave Synthesizer made by [Crystal Semiconductor Corporation](https://en.wikipedia.org/wiki/Crystal_Semiconductor) is a [software synthesizer](https://en.wikipedia.org/wiki/Software_synthesizer) that was bundled with some sound cards.

This project uses a decompiled version of the synthesizer to build some tools (like Linux ALSA driver).

Differences from original code:

* original code has some bugs which are fixed in the decompiled version
* original code only uses 22050 Hz output rate, the decompiled version offers wider range of frequencies
* original code uses only one voice for every instrument, the decompiled version also uses second voice for instruments with two voices
  * if you want to hear only one voice then choose original mode instead of the default enhanced mode
* original code uses different rounding when converting floating point numbers to integer

The source code is released with [MIT license](https://spdx.org/licenses/MIT.html).

<hr/>

The projects consists of following parts:

* **synth**
  * Decompiled version of CrystalWave Synthesizer v2.30.
  * This allows using the software synthesizer on other *CPU* architectures (32-bit, 64-bit) and platforms.
* **cws_alsadrv**
  * Linux daemon which provides [ALSA](https://en.wikipedia.org/wiki/Advanced_Linux_Sound_Architecture) MIDI sequencer interface using *synth*.
  * It requires the CrystalWave Synthesizer datafile *cwcwtsmp.dat*.
* **cws_coredrv**
  * MacOS daemon which provides Core MIDI synthesizer using *synth*.
  * It requires the CrystalWave Synthesizer datafile *cwcwtsmp.dat*.
* **cws_pcmconvert**
  * Tool to convert [Standard MIDI File](https://www.midi.org/specifications-old/item/standard-midi-files-smf) to *PCM* (*WAV* or *RAW*) using *synth*.
  * It requires the CrystalWave Synthesizer datafile *cwcwtsmp.dat*.
* **datafile**
  * CrystalWave Synthesizer datafile *cwcwtsmp.dat*
* **documentation**
  * MIDI implementation (unofficial)

## Compilation

Use appropriate Makefile in *cws_alsadrv* / *cws_coredrv* / *cws_pcmconvert* to compile the programs.
