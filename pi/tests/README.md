# Pi tests

Run deterministic tests from the repository root with:

```bash
python -m unittest discover -s pi/tests
```

Place real Pi-to-MCU bench checks under `integration/`. They require connected
hardware and are not substitutes for the deterministic protocol tests.
