# TID clock (and other watchfaces for pebble)

Use [rebbletool](https://github.com/richinfante/rebbletool) to build and install on your Pebble or in an emulator on a modern computer.

## Overview

TID clock is a Pebble watchface that displays:

- The IATA code and airport name whose local time is closest to noon.
- The current time in decimal format (TID).
- Internet time (.beats).

## Prerequisites

- A Pebble watch or a compatible emulator (e.g., Basalt).
- [Rebbletool](https://github.com/richinfante/rebbletool) installed and configured.
- Python 3 (for timezone data generation).


## For Python

Create a virtual environment (using [`uv`](https://github.com/astral-sh/uv?tab=readme-ov-file#installation)) and install dependencies:

```bash
uv venv
source .venv/bin/activate
uv pip install -r requirements.txt
```

## Build & Installation

Use the included `run.sh` helper script to streamline common tasks:

```bash
# 1) Generate timezone and airport data
./run.sh generate

# 2) Build the project
./run.sh build

# 3) Install onto the emulator (Basalt)
./run.sh install

# 4) Build and install in one step
./run.sh debug
```

Or run each step manually:

```bash
# Generate the airport timezone list
python3 generate_airport_tz_list.py --top 10 --max-bucket 10 --out src/c/airport_tz_list.c

# Build using Rebbletool
rebble build

# Install to device or emulator
rebble install --emulator basalt
```
