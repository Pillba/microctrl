"""Launch the isolated development dispense trigger.

Examples:
    python3 test_dispense_trigger.py
    python3 test_dispense_trigger.py --auto-trigger
    python3 test_dispense_trigger.py --port /dev/ttyACM0
"""

from dev_dispense import main


if __name__ == "__main__":
    raise SystemExit(main())
