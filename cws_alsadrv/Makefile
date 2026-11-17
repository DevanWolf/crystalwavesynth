all: cws_alsadrv

cws_alsadrv: cws_alsadrv.c ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c ../synth/cwssynth.h
	$(CC) -O2 -Wall -o cws_alsadrv cws_alsadrv.c ../synth/cwssynth.c ../synth/cwssynthx.c ../synth/cwsreverb.c -I../synth -lasound -lpthread

.PHONY: clean
clean:
	rm -f cws_alsadrv
