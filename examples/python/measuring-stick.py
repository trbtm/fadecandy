#!/usr/bin/env python

# Open Pixel Control version of the "measuring_stick" Arduino sketch:
# For each group of 64 LEDs (one strip), lights all LEDs with every
# multiple of 10 lit green.

import opc, time

numStrings = 6
client = opc.Client('localhost:7890')

string = [ (128, 128, 128) ] * 120
for i in range(12):
	string[10 * i] = (128, 255, 128)

# Immediately display new frame
pixels = string * numStrings
client.put_pixels(pixels)
client.put_pixels(pixels)
