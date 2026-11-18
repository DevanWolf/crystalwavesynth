# MIDI Implementation

## Channel Voice Messages

|MIDI Message|HEX Code|Description|Compatibility|
|:-|:-|:-|:-:|
|NOTE ON|9nH kk vv|MIDI channel n(0-15) note ON #kk(0-127), velocity vv(1-127).<br>vv=0 means NOTE OFF|MIDI|
|NOTE OFF|8nH kk vv|MIDI channel n(0-15) note OFF #kk(0-127), vv is ignored.|MIDI|
|PITCH BEND|EnH bl bh|Pitch bend as specified by bh\|bl (14 bits).<br>Maximum swing is +/- 2 tone (power-up).<br>Can be changed using « Pitch bend sensitivity ».<br>Center position is 00H 40H.|GM|
|PROGRAM<br>CHANGE|CnH pp|Program (patch) change.<br>Specific action on channel 10 (n=9) : no change.|GM|
|CTRL 06|BnH 06H cc|Data entry MSB : provides data to RPN and NRPN|MIDI|
|CTRL 07|BnH 07H cc|Volume|MIDI|
|CTRL 10|BnH 0AH cc|Pan|MIDI|
|CTRL 11|BnH 0BH cc|Expression|MIDI/GM|
|CTRL 38|BnH 26H cc|Data entry LSB : provides data to RPN and NRPN|MIDI|
|CTRL 64|BnH 40H cc|Sustain (damper) pedal|MIDI|
|CTRL 96|BnH 60H xx|Increase RPN/NRPN value|MIDI|
|CTRL 97|BnH 61H xx|Decrease RPN/NRPN value|MIDI|
|CTRL 98|BnH 62H xx|NRPN LSB|MIDI|
|CTRL 99|BnH 63H xx|NRPN MSB|MIDI|
|CTRL 100|BnH 64H vv|RPN LSB|MIDI|
|CTRL 101|BnH 65H vv|RPN MSB|MIDI|
|CTRL 120|BnH 78H xx|All sounds off (abrupt stop of sound on channel n)<br>The same processing will be carried out as when « All Notes Off » is received.|MIDI|
|CTRL 121|BnH 79H xx|Reset all controllers|MIDI|
|CTRL 123|BnH 7BH xx|All notes off|MIDI|
|CTRL 124|BnH 7CH xx|Omni off.<br>The same processing will be carried out as when « All Notes Off » is received.|MIDI|
|CTRL 125|BnH 7DH xx|Omni on.<br>The same processing will be carried out as when « All Notes Off » is received.|MIDI|
|CTRL 126|BnH 7EH xx|Mono on.<br>The same processing will be carried out as when « All Notes Off » is received.|MIDI|
|CTRL 127|BnH 7FH xx|Poly on.<br>The same processing will be carried out as when « All Notes Off » is received.|MIDI|
|RPN 0000H|BnH 65H 00H 64H 00H 06H vv|Pitch bend sensitivity in semitones #vv(0-127)|MIDI/GM|
|RPN 0001H|BnH 65H 00H 64H 01H 06H vh 26H vl|Fine tuning in cents (vh,vl=00 00H -100, vh,vl=40 00H 0, vh,vl=7F 7FH +99.99)|MIDI|
|RPN 0002H|BnH 65H 00H 64H 02H 06H vv|Coarse tuning in half-tones (vv=28H -24, vv=40H 0, vv=58H +24)|MIDI|
|SYSTEM RESET|FFH|Stop all sound and reset all parameters to its initial values|MIDI|

## System Exclusive Messages

None supported