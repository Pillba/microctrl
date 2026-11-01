# Copyright (c) 2014 Adafruit Industries
# Author: Tony DiCola
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
import numbers
import time
import numpy as np

import spidev
import RPi.GPIO as GPIO


__version__ = '0.0.4'

BG_SPI_CS_BACK = 0
BG_SPI_CS_FRONT = 1

SPI_CLOCK_HZ = 16000000

# Commands as listed in ST7796S Datasheet page 131
# TODO: rename from "ST7789" to something

CMD_NOP = 0x00      # No Operation
CMD_SWRESET = 0x01  # Software reset
CMD_RDDID = 0x04    # Read display ID
CMD_RDDSI = 0x05    # Read DSI
CMD_RDDST = 0x09    # Read display status
CMD_RDDPM = 0x0A    # Read display power
CMD_RDDMADCTL = 0x0B # Read display
CMD_RDDIPF = 0x0C   # Read display pixel
CMD_RDDIM = 0x0D    # Read display image
CMD_RDDSM = 0x0E    # Read display signal
CMD_RDDSDR = 0x0F   # Read display self-diagnostic result

CMD_SLPIN = 0x10    # Sleep in
CMD_SLPOUT = 0x11   # Sleep out
CMD_PTLON = 0x12    # Partial mode on
CMD_NORON = 0x13    # Partial off (normal)

CMD_INVOFF = 0x20   # Display inversion off
CMD_INVON = 0x21    # Display inversion on
CMD_DISPOFF = 0x28  # Display off
CMD_DISPON = 0x29   # Display on

CMD_CASET = 0x2A    # Column address set
CMD_RASET = 0x2B    # Row address set
CMD_RAMWR = 0x2C    # Memory write
CMD_RAMRD = 0x2E    # Memory read

CMD_PTLAR = 0x30    # Partial start/end address set
CMD_VSCRDEF = 0x33  # Vertical scrolling definition
CMD_TEOFF = 0x34    # Tearing effect line off
CMD_TEON = 0x35     # Tearing effect line on
CMD_MADCTL = 0x36   # Memory data access control
CMD_VSCRSADD = 0x37 # Vertical scrolling start address
CMD_IDMOFF = 0x38   # Idle mode off
CMD_IDMON = 0x39    # Idle mode on
CMD_COLMOD = 0x3A   # Interface pixel format
CMD_RAMWRC = 0x3C   # Memory write continue
CMD_RAMRDC = 0x3E   # Memory read continue

CMD_TESCAN = 0x44   # Set tear scanline
CMD_RDTESCAN = 0x44 # Get scanline

CMD_WRDISBV = 0x51  # Write display brightness
CMD_RDDISBV = 0x52  # Read display brightness
# 53/54 write/read ctrl display
# 55/56 write/read content adaptive brightness control
# 5E/5F write/read CABC minimum brightness

CMD_RDFCHKSUM = 0xAA    # Read first checksum
CMD_RDCCHKSUM = 0xAF    # Read continue checksum

# Not sure why 0xD_ is here instead of after 0xC_, but whatever
CMD_RDID1 = 0xDA    # Read ID1
CMD_RDID2 = 0xDB    # Read ID2
CMD_RDID3 = 0xDC    # Read ID3

CMD_IFMODE = 0xB0   # Interface mode control
CMD_FRMCTR1 = 0xB1  # Frame rate control (in normal mode)
CMD_FRMCTR2 = 0xB2  # Frame rate control (in idle mode)
CMD_FRMCTR3 = 0xB3  # Frame rate control (in partial mode)
CMD_DIC = 0xB4      # Display inversion control
CMD_BPC = 0xB5      # Blinking porch control
CMD_DFC = 0xB6      # Display function control
CMD_EM = 0xB7       # Entry mode set
#ST7789_DISSET5 = 0xB6

#ST7789_GCTRL = 0xB7
#ST7789_GTADJ = 0xB8
#ST7789_VCOMS = 0xBB

CMD_PWR1 = 0xC0     # Power control 1
CMD_PWR2 = 0xC1     # Power control 2
CMD_PWR3 = 0xC2     # Power control 3
CMD_VCMPCTL = 0xC5  # Vcom control
CMD_VCMOR = 0xC6    # Vcom offset register

#ST7789_LCMCTRL = 0xC0
#ST7789_IDSET = 0xC1
#ST7789_VDVVRHEN = 0xC2
#ST7789_VRHS = 0xC3
#ST7789_VDVS = 0xC4
#ST7789_VMCTR1 = 0xC5
#ST7789_FRCTRL2 = 0xC6
#ST7789_CABCCTRL = 0xC7

CMD_NVMADW = 0xD0   # NVM address/data
CMD_NVMBPROG = 0xD1 # NVM byte program control
CMD_NVMSTRD = 0xD2  # NVM status read
CMD_RDID4 = 0xD3    # Read ID4 (WHYYYY IS THIS NOT WITH THE OTHER RDID COMMANDS)

#ST7789_RDID1 = 0xDA
#ST7789_RDID2 = 0xDB
#ST7789_RDID3 = 0xDC
#ST7789_RDID4 = 0xDD

CMD_PGC = 0xE0      # Positive gamma control
CMD_NGC = 0xE1      # Negative gamma control
CMD_DGC1 = 0xE2     # Digital gamma control 1
CMD_DGC2 = 0xE3     # Digital gamma control 2
CMD_DOCA = 0xE8     # Display output ctrl adjust

CMD_CSCON = 0xF0    # Command set control
CMD_SPIRC = 0xFB    # SPI read control

#ST7789_GMCTRP1 = 0xE0
#ST7789_GMCTRN1 = 0xE1

# ST7789_PWCTR6 = 0xFC


class ST7796(object):
    """Representation of an ST7796 TFT LCD."""

    def __init__(self, port, cs, dc, backlight=None, rst=None, width=480,
                 height=320, rotation=90, invert=True, spi_speed_hz=16000000,
                 offset_left=0,
                 offset_top=0):
        """Create an instance of the display using SPI communication.

        Must provide the GPIO pin number for the D/C pin and the SPI driver.

        Can optionally provide the GPIO pin number for the reset pin as the rst parameter.

        :param port: SPI port number (0 or 1 on RPi)
        :param cs: SPI chip-select number (0 or 1)
        :param backlight: Pin for controlling backlight, not used
        :param rst: Reset pin for ST7796
        :param width: Width of display connected to ST7796
        :param height: Height of display connected to ST7796
        :param rotation: Rotation of display connected to ST7796
        :param invert: Invert display
        :param spi_speed_hz: SPI speed (in Hz)

        """
        if rotation not in [0, 90, 180, 270]:
            raise ValueError("Invalid rotation {}".format(rotation))

        if width != height and rotation in [90, 270]:
            raise ValueError("Invalid rotation {} for {}x{} resolution".format(rotation, width, height))

        GPIO.setwarnings(False)
        GPIO.setmode(GPIO.BCM)

        self._spi = spidev.SpiDev(port, cs)
        self._spi.mode = 0
        self._spi.lsbfirst = False
        self._spi.max_speed_hz = spi_speed_hz

        self._dc = dc
        self._rst = rst
        self._width = width
        self._height = height
        self._rotation = rotation
        self._invert = invert

        self._offset_left = offset_left
        self._offset_top = offset_top

        # Set DC as output.
        GPIO.setup(dc, GPIO.OUT)

        # Setup backlight as output (if provided).
        self._backlight = backlight
        if backlight is not None:
            GPIO.setup(backlight, GPIO.OUT)
            GPIO.output(backlight, GPIO.LOW)
            time.sleep(0.1)
            GPIO.output(backlight, GPIO.HIGH)

        # Setup reset as output (if provided).
        if rst is not None:
            GPIO.setup(self._rst, GPIO.OUT)
            self.reset()
        self._init()

    def send(self, data, is_data=True, chunk_size=4096):
        """Write a byte or array of bytes to the display. Is_data parameter
        controls if byte should be interpreted as display data (True) or command
        data (False).  Chunk_size is an optional size of bytes to write in a
        single SPI transaction, with a default of 4096.
        """
        # Set DC low for command, high for data.
        GPIO.output(self._dc, is_data)
        # Convert scalar argument to list so either can be passed as parameter.
        if isinstance(data, numbers.Number):
            data = [data & 0xFF]
        # Write data a chunk at a time.
        for start in range(0, len(data), chunk_size):
            end = min(start + chunk_size, len(data))
            self._spi.xfer(data[start:end])

    def set_backlight(self, value):
        """Set the backlight on/off."""
        if self._backlight is not None:
            GPIO.output(self._backlight, value)

    @property
    def width(self):
        return self._width if self._rotation == 0 or self._rotation == 180 else self._height

    @property
    def height(self):
        return self._height if self._rotation == 0 or self._rotation == 180 else self._width

    def command(self, data):
        """Write a byte or array of bytes to the display as command data."""
        self.send(data, False)

    def data(self, data):
        """Write a byte or array of bytes to the display as display data."""
        self.send(data, True)

    def reset(self):
        """Reset the display, if reset pin is connected."""
        if self._rst is not None:
            GPIO.output(self._rst, 1)
            time.sleep(0.500)
            GPIO.output(self._rst, 0)
            time.sleep(0.500)
            GPIO.output(self._rst, 1)
            time.sleep(0.500)

    def _init(self):
        # Initialize the display.
        self.command(CMD_SWRESET)   # Software reset
        time.sleep(0.150)           # delay 150 ms

        self.command(CMD_CSCON)     # Command set control
        self.data(0xC3)             # Enable command 2 part I
        self.command(CMD_CSCON)     # Command set control
        self.data(0x96)             # Enable command 2 part II

        # Arduino has this commented out: 0xC0 0x10 0x10 (power control 1)
        # Seems like it's for tuning voltage of something

        # 0xC1 0x41 commented out

        self.command(CMD_VCMPCTL)  # 0xC5
        self.data(0x1C) # VCOM = 1.000

        self.command(CMD_MADCTL) # Memory data access control
        self.data(0x48) # Affects column address order and LCD vertical refresh direction

        # Interface pixel format 0x3A 0x55
        self.command(CMD_COLMOD)
        self.data(0x55) # 16 bits per pixel for RGB and control interface color formats

        # Interface mode control 0xB0 0x80
        self.command(CMD_IFMODE)
        self.data(0x80) # Do not use DOUT pin on driver

        # Commented out: frame rate control 0xb1 0xb0 0x11

        # Inversion control
        self.command(CMD_DIC)
        self.data(0x01) # 1-dot inversion

        # Display function control 0xb6 0x80 0x02 0x3b
        self.command(CMD_DFC)
        self.data(0x80) # Display direct to shift register
        self.data(0x02) # Does something to the scan cycle idk
        self.data(0x3b) # Drive 480 lines

        # Entry mode 0xb7 0xc6
        self.command(CMD_EM)
        self.data(0xC6) # EPF data format for convertion 16bit to 18bit; not in deep sleep mode; gate output normal display

        # Commented out: interlace pixel format 0x31 0x66
        # Commented out: adjustment control 0xf7 0xa9 0x51 0x2c 0x82

        self.command(CMD_CSCON)     # Command set control
        self.data(0x69)             # Disable command 2 part II
        self.command(CMD_CSCON)     # Command set control
        self.data(0x3C)             # Disable command 2 part I

        self.command(CMD_SLPOUT)    # Disable sleep
        time.sleep(0.150)           # delay 150 ms

        self.command(CMD_DISPON)    # Main screen turn on 0x29
        time.sleep(0.150)           # delay 150 ms


    def begin(self):
        """Set up the display

        Deprecated. Included in __init__.

        """
        pass

    def set_window(self, x0=0, y0=0, x1=None, y1=None):
        """Set the pixel address window for proceeding drawing commands. x0 and
        x1 should define the minimum and maximum x pixel bounds.  y0 and y1
        should define the minimum and maximum y pixel bound.  If no parameters
        are specified the default will be to update the entire display from 0,0
        to width-1,height-1.
        """
        if x1 is None:
            x1 = self._width - 1

        if y1 is None:
            y1 = self._height - 1

        y0 += self._offset_top
        y1 += self._offset_top

        x0 += self._offset_left
        x1 += self._offset_left

        self.command(CMD_CASET)       # Column addr set
        self.data(x0 >> 8)
        self.data(x0 & 0xFF)             # XSTART
        self.data(x1 >> 8)
        self.data(x1 & 0xFF)             # XEND
        self.command(CMD_RASET)       # Row addr set
        self.data(y0 >> 8)
        self.data(y0 & 0xFF)             # YSTART
        self.data(y1 >> 8)
        self.data(y1 & 0xFF)             # YEND
        self.command(CMD_RAMWR)       # write to RAM

    def display(self, image):
        """Write the provided image to the hardware.

        :param image: Should be RGB format and the same dimensions as the display hardware.

        """
        # Set address bounds to entire display.
        self.set_window()

        # Convert image to 16bit RGB565 format and
        # flatten into bytes.
        pixelbytes = self.image_to_data(image, self._rotation)

        # Write data to hardware.
        for i in range(0, len(pixelbytes), 4096):
            self.data(pixelbytes[i:i + 4096])

    def image_to_data(self, image, rotation=0):
        if not isinstance(image, np.ndarray):
            image = np.array(image.convert('RGB'))

        # Rotate the image
        pb = np.rot90(image, rotation // 90).astype('uint16')

        # Mask and shift the 888 RGB into 565 RGB
        red   = (pb[..., [0]] & 0xf8) << 8
        green = (pb[..., [1]] & 0xfc) << 3
        blue  = (pb[..., [2]] & 0xf8) >> 3

        # Stick 'em together
        result = red | green | blue

        # Output the raw bytes
        return result.byteswap().tobytes()
