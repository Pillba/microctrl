# Raspberry Pi application

This is the Pi-side starting point based on the existing panel-ui application.
Run it from `pi/src/` after installing [`requirements.txt`](requirements.txt):

```bash
cd pi/src
python main.py
```

The application owns the UI, serial worker, protocol implementation, and
SQLite access. It does not control MCU peripheral pins directly.
