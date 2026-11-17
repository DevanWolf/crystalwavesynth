all: cws_coredrv

cws_coredrv: cws_coredrv.c ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c ../synth/cwssynth.h
	$(CC) -O2 -Wall -o cws_coredrv cws_coredrv.c ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c -I../synth -lpthread -framework CoreMIDI -framework CoreFoundation -framework AudioToolbox

.PHONY: clean
clean:
	rm -f cws_coredrv
