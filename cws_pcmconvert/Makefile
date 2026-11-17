all: cws_pcmconvert

cws_pcmconvert: cws_pcmconvert.c midi_loader.c midi_loader.h ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c ../synth/cwssynth.h
	$(CC) -O2 -Wall -o cws_pcmconvert cws_pcmconvert.c midi_loader.c ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c -I../synth

.PHONY: clean
clean:
	rm -f cws_pcmconvert
