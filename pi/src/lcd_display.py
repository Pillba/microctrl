from __future__ import annotations

from PIL import Image
from PyQt5.QtGui import QImage

import ST7796


class ST7796Display:
    def __init__(self) -> None:
        self._display = ST7796.ST7796(
            height=480,
            width=320,
            rotation=180,
            port=0,
            cs=0,
            rst=26,
            dc=25,
            spi_speed_hz=16000000,
            offset_left=0,
            offset_top=0,
        )

    def show(self, image: Image.Image) -> None:
        frame = image.convert("RGB")
        if frame.size != (320, 480):
            frame = frame.resize((320, 480))
        self._display.display(frame)


def qimage_to_pil(image: QImage) -> Image.Image:
    rgb_image = image.convertToFormat(QImage.Format_RGB888)
    width = rgb_image.width()
    height = rgb_image.height()
    stride = rgb_image.bytesPerLine()
    pixels = rgb_image.bits()
    pixels.setsize(rgb_image.byteCount())

    return Image.frombytes(
        "RGB",
        (width, height),
        bytes(pixels),
        "raw",
        "RGB",
        stride,
    )
