# UART sanity bench

Proves the MCU's UART link on its own: no steppers, servo, pump or scale. All
of those are held off, so a fault that remains is in the clock, the UART wiring
or the adapter.

Upload from `microctrl/` through the Arduino-as-ISP:

```bash
pio run -d firmware/bench/uart_sanity -e pcb -t upload        # 115200, like production
pio run -d firmware/bench/uart_sanity -e pcb_9600 -t upload   # 9600, if 115200 shows garbage
```

Watch it on the Pi (DTR/RTS left alone):

```bash
python3 -m serial.tools.miniterm --dtr 0 --rts 0 /dev/ttyUSB0 115200
```

Expected:

```text
PILLBA UART SANITY baud=115200 f_cpu=16000000 reset=POR
ALIVE 1 up=500
ALIVE 2 up=1000
```

Type into miniterm and the MCU answers `RX <hex bytes>`, which proves the
Pi → MCU direction.

| You see | Meaning |
| --- | --- |
| Clean lines | Clock, UART and wiring are fine; the problem is in production or what it drives |
| `ALIVE` restarts at 1, `reset=BOD` | Supply dips below the brown-out level |
| `reset=EXT` repeatedly | Something pulls RESET low (DTR/RTS wiring, programmer) |
| Garbage at 115200, clean at 9600 | Baud error or slow edges: clock or level shifting |
| Garbage at both | MCU not running at 16 MHz (check lfuse = 0xFF) or wrong logic levels |
| Only `FF` or nothing | MCU TX is not reaching the adapter's RX |

Upload production again afterwards: `pio run -d firmware -e production -t upload`.
