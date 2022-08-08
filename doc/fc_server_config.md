Fadecandy: Server Configuration
===============================

The Fadecandy Server is configured with a JSON object. There is a default configuration built-in, which you can see by running `fcserver -h`. A copy of this configuration is also included as [examples/config/default.json](https://github.com/scanlime/fadecandy/blob/master/examples/config/default.json).

A new configuration file can be given to `fcserver` on the command line in the following manner:

` $ <path to fcserver> <path to config.json file> `

You can use one of the examples as a starting point. Typically the default configuration will work for any single-controller setup. If you're using multiple controllers or there are other options you want to tweak, you'll want to create an fcserver configuration file for your project.

Parts of the JSON config format are shared with the network protocols. For example, color correction data and device information are stored in a common format. Some parts of the JSON configuration file can be modified at runtime.

Top-level Object
----------------

The configuration file is a JSON object. By default, it looks like this:

```
{
    "listen": ["127.0.0.1", 7890],
    "relay": NULL,
    "verbose": true,

    "color": {
        "gamma": 2.5,
        "whitepoint": [1.0, 1.0, 1.0]
    },

    "devices": [
        {
            "type": "fadecandy",
            "map": [
                [ 0, 0, 0, 512 ]
            ]
        }
    ]
}
```

Name     | Summary
-------- | -------------------------------------------------------
listen   | What address and port should the server listen on?
relay    | What address and port should the server relay messages to?
verbose  | Does the server log anything except errors to the console?
color    | Default global color correction settings
devices  | List of configured devices

Listen
------

By default, fcserver listens on port 7890 on the local (loopback) interface. This server will not be reachable by other computers on your network, only by programs on the same computer.

The "listen" configuration key must be a JSON array of the form [**host**, **port**], where **port** is a number and **host** is either a string or *null*. If the host is *null*, fcserver listens on all network interfaces and it's reachable from other computers

*Warning:* Do not run fcserver on an untrusted network. It has no built-in security provisions, so anyone on your network will have control of fcserver. Additionally, bugs in fcserver may compromise the security of your computer.

Relay
-----

The "relay" configuration key is using the same format as the "listen" configuration key and allows clients to connect on a separate socket to receive a copy of the OPC messages the fcserver is handling.

Relaying is disabled by default.

Color
-----

The "color" configuration key is a JSON object containing color correction data. This color correction data can also be changed at runtime by both the OPC and WebSocket interfaces. If instead of an object "color" is null, a linear (identity mapped) color table is used.

The default color configuration is:

```
"color": {
    "gamma": 2.5,
    "whitepoint": [0.98, 1.0, 1.0],
    "linearSlope": 1.0,
    "linearCutoff": 0.0
},
```

Supported keys in the "color" object are:

Name         | Description
------------ | -----------------------------------------------------------------
gamma        | Exponent for the nonlinear portion of the brightness curve
whitepoint   | Vector of [red, green, blue] values to multiply by colors prior to gamma correction
linearSlope  | Slope (output / input) of the linear section of the brightness curve
linearCutoff | Y (output) coordinate of intersection between linear and nonlinear curves

By default, brightness curves are entirely nonlinear. By setting linearCutoff to a nonzero value, though, a linear area may be defined at the bottom of the brightness curve.

The linear section, near zero, avoids creating very low output values that will cause distracting flicker when dithered. This isn't a problem when the LEDs are viewed indirectly such that the flicker is below the threshold of perception, but in cases where the flicker is a problem this linear section can eliminate it entierly at the cost of some dynamic range. To enable the linear section, set linearCutoff to some nonzero value. A good starting point is 1/256.0, correspnding to the lowest 8-bit PWM level.

Devices
-------

The JSON configuration file is a dictionary which contains global configuration and an array of device objects.

For each device, a dictionary includes device properties as well as a mapping table with commands which wire outputs to their corresponding OPC inputs. The map is a list of objects which act as mapping commands.

Fadecandy Devices
----------------- 

Supported mapping objects for Fadecandy devices:

* [ *OPC Channel*, *First OPC Pixel*, *First output pixel*, *Pixel count* ]
    * Map a contiguous range of pixels from the specified OPC channel to the current device
    * For Fadecandy devices, output pixels are numbered from 0 through 511. Strand 1 begins at index 0, strand 2 begins at index 64, etc.
* [ *OPC Channel*, *First OPC Pixel*, *First output pixel*, *Pixel count*, *Color channels* ]
    * As above, but the mapping between color channels and WS2811 output channels can be changed.
    * The "Color channels" must be a 3-letter string, where each letter corresponds to one of the WS2811 outputs.
    * Each letter can be "r", "g", or "b" to choose the red, green, or blue channel respectively, or "l" to use the average luminosity.

If the pixel count is negative, the output pixels are mapped in reverse order starting at the first output pixel index and decrementing the index for each successive pixel up to the absolute value of the pixel count.

Other settings for Fadecandy devices:

Name         | Values               | Default | Description
------------ | -------------------- | ------- | --------------------------------------------
led          | true / false / null  | null    | Is the LED on, off, or under automatic control?
dither       | true / false         | true    | Is dithering enabled?
interpolate  | true / false         | true    | Is inter-frame interpolation enabled?

The following example config file supports two Fadecandy devices with distinct serial numbers. They both receive data from OPC channel #0. The first 512 pixels map to the first Fadecandy device. The next 64 pixels map to the entire first strand of the second Fadecandy device, the next 32 pixels map to the beginning of the third strand with the color channels in Blue, Green, Red order, and the next 32 pixels map to the end of the third strand in reverse order.

    {
        "listen": ["127.0.0.1", 7890],
        "verbose": true,

        "color": {
            "gamma": 2.5,
            "whitepoint": [0.98, 1.0, 1.0]
        },

        "devices": [
            {
                "type": "fadecandy",
                "serial": "FFFFFFFFFFFF00180017200214134D44",
                "led": false,
                "map": [
                    [ 0, 0, 0, 512 ]
                ]
            },
            {
                "type": "fadecandy",
                "serial": "FFFFFFFFFFFF0021003B200314134D44",
                "map": [
                    [ 0, 512, 0, 64 ],
                    [ 0, 576, 128, 32, "bgr" ],
                    [ 0, 608, 191, -32 ]
                ]
            }
        ]
    }

Glimmer Devices
--------------- 

Glimmer is an updated firmware for the Fadecandy board that supports bigger LED matrices than
the original firmware while using the same hardware. The temporal dithering, interpolation,
and color correction algorithms have been redesigned to free up more memory for pixels and to
allow for more flexible use of on-board resources.

**Matrix Size**

Glimmer supports matrices with up to 8 strips of 120 pixels depending on how it is configured.
Some combinations of settings use more memory and therefore may reduce the total number of
pixels that can be supported.

Factors that influence memory usage:

- Strip length: Glimmer needs 48 bytes per unit of strip length for its DMA buffers. By default,
  this buffer is sized to support strips of up to 120 LEDs but it can be changed by modifying
  the firmware.

- Interpolation: Glimmer uses triple-buffering when interpolation is enabled and double-buffering
  when interpolation is enabled. The size of the frame buffer is determined by the total number
  of pixels in the matrix and the color depth.

- Color depth: Glimmer represents each color corrected pixel in either 24 or 33 bits (8 or 11 bits
  per component). Using 33 bits per pixel yields better fidelity and smoother transitions when
  dithering is enabled (especially for dim colors) but it also increases the size of the frame
  buffers by about 40%.

Note that Glimmer's implementation of temporal dithering does not require any extra memory.

This table summarizes the supported combinations:

Color Depth | Interpolation | Max Total Pixels | Example Matrix |
----------- | ------------- | ---------------- | -------------- |
24 bits     | no            | 960              | 8 * 120        |
24 bits     | yes           | 960              | 8 * 120        |
33 bits     | no            | 960              | 8 * 120        |
33 bits     | yes           | 720              | 6 * 120        |

If you need an even bigger matrix, you can edit Glimmer's "config.h" header file and recompile
the firmware to make different memory allocation and image quality trade-offs.

**Refresh Rate and Overclocking**

Glimmer renders freshly dithered and interpolated video frames continously as fast as the LED
strips can handle to ensure smooth transitions. Longer strips take more time to refresh
so care must be taken to prevent artifacts, such as flickering, from occurring.

Glimmer supports a variety of WS2811, WS2812B, WS2813, SK6812, and similar individually
addressable LED strips. By default, these strips operate at 800 kilobits per second (about 30000
pixels refreshed per second). As it happens, there's some wiggle room in the timings so
certain strips can be overclocked to produce faster frame rates which is particularly
beneficial when working with longer strips.

The following table illustrates the influence of LED strip timings and length on refresh rates
(faster is better):

Bitrate  | Reset Interval | Strip Length | Refresh Rate |
-------- | -------------- | ------------ | ------------ |
800 kbps | 300 us         | 64 leds      | 450 Hz       |
800 kbps | 100 us         | 64 leds      | 495 Hz       |
900 kbps | 100 us         | 64 leds      | 553 Hz       |
1 Mbps   | 80 us          | 64 leds      | 618 Hz       |
800 kbps | 300 us         | 120 leds     | 256 Hz       |
800 kbps | 100 us         | 120 leds     | 270 Hz       |
900 kbps | 100 us         | 120 leds     | 303 Hz       |
1 Mbps   | 80 us          | 120 leds     | 337 Hz       |

*Formula: `refresh_rate_in_hz = 1000000 / (strip_length * 24 * 1000000 / bitrate_in_bps + reset_interval_in_us)`*

The default timings are intentionally conservative (slow) and should work for most strips.
Use the `timings` parameter to specify one of the built-in named timings in the table
below as a string or custom timings using the format: `[ freq, ri, t0h, t1h ]`

* freq: The bitrate in Hz, typically about 400 or 800
* ri: The reset interval in microseconds to wait between successive refreshes, typically
      between 50 and 300
* t0h: The proportion (out of 255) of the waveform to hold the output pin high to signal a zero bit,
       needs to be short enough to be clearly distinguished from a one bit by the LED's circuitry
* t1h: The proportion (out of 255) of the waveform to hold the output pin high to signal a one bit,
       needs to be long enough to be clearly distinguished from a zero bit by the LED's circuitry

Timing Name       | Equivalent Definition     |
----------------- | ------------------------- |
default           | [ 800000, 300, 60, 176 ]  |
sk6812            | [ 800000, 100, 60, 176 ]  |
sk6812-fast       | [ 900000, 100, 44, 150 ]  |
sk6812-extreme    | [ 960000, 80, 50, 160 ]  |

With a little care, you may be able to increase your refresh rates by quite a lot. In general,
`t0h` and `t1h` must be reduced as `freq` is increased. If the LEDs starts flickering uncontrollably,
then you've gone too far.

Your mileage may vary!

**Temporal Dithering**

Glimmer dithers frames by adding a tiny amount of periodic noise to each color component
of each pixel on each frame to increase the effective color resolution of the LED strips
by a few bits. Dithering is especially beneficial for reproducing very dim shades.

In effect, dithering causes pixels to rapidly switch between two very slightly different
colors. When the switching happens quickly enough, your eyes perceive a color in-between
the two shades. When the switching happens too slowly, your eyes may perceive a flickering
which can be very distracting. 

For dithering to work well, there must be enough color resolution available to render
in-between colors such as when using interpolation or a frame buffer with more than 8 bits
per color component. The period of the dithering noise (the number of bits to dither) must
also be selected to minimize flickering at the intended refresh rate.

The following table shows the relationship between refresh rate, dither bits, and flicker
frequency.

| Refresh Rate | Dither Bits | Flicker Frequency            |
| ------------ | ----------- | ---------------------------- |
| 256 Hz       | 1           | 128 Hz (imperceptible)       |
| 256 Hz       | 2           | 64 Hz (slight)               |
| 256 Hz       | 3           | 32 Hz (severe)               |
| 256 Hz       | 4           | 16 Hz (strobing)             |
| 337 Hz       | 1           | 168 Hz (imperceptible)       |
| 337 Hz       | 2           | 84 Hz (very slight)          |
| 337 Hz       | 3           | 42 Hz (moderate)             |
| 337 Hz       | 3           | 21 Hz (severe)               |
| 450 Hz       | 1           | 225 Hz (imperceptible)       |
| 450 Hz       | 2           | 112 Hz (imperceptible)       |
| 450 Hz       | 3           | 56 Hz (slight)               |
| 450 Hz       | 4           | 28 Hz (severe)               |

*Formula: `flicker_in_hz = refresh_rate_in_hz / 2^dither_bits`*

To minimize perceptible flickering due to dithering, do the following:

* Optimize your strip timings (optional).
* Determine your refresh rate using the formula in the prior section.
* Using the formula above, choose a value for dither bits that yields a flicker
  frequency no less than about 40 Hz.
* Set the value of `ditherBits` in the configuration accordingly or disable
  dithering entirely if you prefer.
* Test the effect visually, especially using very dim scenes. Watch for any uncomfortable
  flickering and adjust as needed.

**Configuration**

Supported mapping objects for Glimmer devices:

* [ *OPC Channel*, *First OPC Pixel*, *First output pixel*, *Pixel count* ]
    * Map a contiguous range of pixels from the specified OPC channel to the current device
    * For Glimmer devices, output pixels are numbered sequentially along each strand. For example,
      if there are 90 LEDs per strand, then Strand 1 begins at index 0, stand 2 begins at index 90, etc.
* [ *OPC Channel*, *First OPC Pixel*, *First output pixel*, *Pixel count*, *Color channels* ]
    * As above, but the mapping between color channels and output channels can be changed.
    * The "Color channels" must be a 3-letter string, where each letter corresponds to one of the outputs.
    * Each letter can be "r", "g", or "b" to choose the red, green, or blue channel respectively, or "l" to use the average luminosity.

If the pixel count is negative, the output pixels are mapped in reverse order starting at the first output pixel index and decrementing the index for each successive pixel up to the absolute value of the pixel count.

Other settings for Glimmer devices:

Name         | Values                | Default   | Description
------------ | --------------------- | --------- | --------------------------------------------
strips       | 1 to 8                | 8         | The number of LED strips wired up to the board
stripLength  | 1 to 255              | 64        | The number of LEDs per strip
led          | true / false / null   | null      | Is the indicator LED on, off, or under automatic control?
dither       | true / false          | true      | Is dithering enabled?
ditherBits   | 0 to 8                | 3         | The maximum number of bits used for temporal dithering
interpolate  | true / false          | true      | Is inter-frame interpolation enabled?
colorDepth   | 24 / 33               | 33        | The number of bits per pixel to send to the board
timings      | *name* or *array*     | "default" | The LED timings, refer to the section about overclocking

The following example config file supports two Glimmer devices with distinct serial numbers. They both receive data from OPC channel #0. The first 720 pixels map to the first Glimmer device which has been overclocked. The next 64 pixels map to the entire first strand of the second Glimmer device, the next 32 pixels map to the beginning of the third strand with the color channels in Blue, Green, Red order, and the next 32 pixels map to the end of the third strand in reverse order.

    {
        "listen": ["127.0.0.1", 7890],
        "verbose": true,

        "color": {
            "gamma": 2.5,
            "whitepoint": [0.98, 1.0, 1.0]
        },

        "devices": [
            {
                "type": "glimmer",
                "serial": "FFFFFFFFFFFF00180017200214134D44",
                "strips": 6,
                "stripLength": 720
                "led": false,
                "timings": "sk6812-extreme",
                "ditherBits": 3,
                "map": [
                    [ 0, 0, 0, 720 ]
                ]
            },
            {
                "type": "glimmer",
                "serial": "FFFFFFFFFFFF0021003B200314134D44",
                "map": [
                    [ 0, 720, 0, 64 ],
                    [ 0, 784, 128, 32, "bgr" ],
                    [ 0, 816, 191, -32 ]
                ]
            }
        ]
    }

**Debugging**

Glimmer offers a few debugging features to assist with firmware development. These flags won't be of
much use during normal operation so they go into a special "debug" section of the configuration.
These flags are subject to change as the firmware evolves.

Name         | Values                | Default   | Description
------------ | --------------------- | --------- | --------------------------------------------
printStats   | true / false          | false     | Print frame timing statistics to the serial port

The following example shows a debug flag

    {
        "devices": [
            {
                "type": "glimmer",
                "map": [
                    [ 0, 0, 0, 512 ]
                ],
                "debug": {
                    "printStats": true
                }
            }
        ]
    }

Using Open Pixel Control with DMX
---------------------------------

The Fadecandy server is designed to make it easy to drive all your lighting via Open Pixel Control, even when you're using a mixture of addressable LED strips and DMX devices.

For DMX, `fcserver` supports the common [Enttec DMX USB Pro adapter](http://www.enttec.com/index.php?main_menu=Products&pn=70304). This device attaches over USB, has inputs and outputs for one DMX universe, and it has an LED indicator. With Fadecandy, the LED will flash any time we process a new frame of video.

The Enttec adapter uses an FTDI FT245 USB FIFO chip internally. For the smoothest USB performance and the simplest configuration, we do not use FTDI's serial port emulation driver. Instead, we talk directly to the FTDI chip using libusb. On Linux this happens without any special consideration. On Mac OS, libusb does not support detaching existing drivers from a device. If you've installed the official FTDI driver, you can temporarily unload it until your next reboot by running:

    sudo kextunload -b com.FTDI.driver.FTDIUSBSerialDriver

Enttec DMX devices can be configured in the same way as a Fadecandy device. For example:

    {
            "listen": [null, 7890],
            "verbose": true,

            "devices": [
                    {
                            "type": "fadecandy",
                            "map": [
                                    [ 0, 0, 0, 512 ]
                            ]
                    },
                    {
                            "type": "enttec",
                            "serial": "EN075577",
                            "map": [
                                    [ 0, 0, "r", 1 ],
                                    [ 0, 0, "g", 2 ],
                                    [ 0, 0, "b", 3 ],
                                    [ 0, 1, "l", 4 ]
                            ]
                    }
            ]
    }

Enttec DMX devices use a different format for their mapping objects:

* [ *OPC Channel*, *OPC Pixel*, *Pixel Color*, *DMX Channel* ]
    * Map a single OPC pixel to a single DMX channel
    * The "Pixel color" can be "r", "g", or "b" to sample a single color channel from the pixel, or "l" to use an average luminosity.
    * DMX channels are numbered from 1 to 512.
* [ *Value*, *DMX Channel* ]
    * Map a constant value to a DMX channel; good for configuration modes

Using Open Pixel Control with the APA102/APA102C/SK9822 
---------------------------------

The Fadecandy server now has experimental support for the APA102 family of LEDs.

APA102 devices can be configured in the same way as a Fadecandy device. For example:

    {
        "listen": ["127.0.0.1", 7890],
        "verbose": true,

        "devices": [
            {
                    "type": "apa102spi",
                    "port": 0,
                    "numLights": 144,
                    "map": [ [ 0, 0, 0, 144 ] ]
                ]
            }
        ]
    }

Supported mapping objects for APA102 devices:

* [ *OPC Channel*, *First OPC Pixel*, *First output pixel*, *Pixel count* ]
    * Map a contiguous range of pixels from the specified OPC channel to the current device
