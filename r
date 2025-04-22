#!/bin/bash

# Exit immediately if a command exits with a non-zero status.
set -e

# Function to run the timezone generation script
generate_tz_list() {
    echo "Generating timezone lists..."
    uv run python scripts/generate_tz_list.py
    uv run python scripts/generate_airport_tz_list.py
}

# Function to build the project
build() {
    echo "Building project..."
    rebble build
}

# Function to install the project on the emulator
install() {
    echo "Installing project on emulator..."
    rebble install --emulator basalt
}

# Function to wipe the emulator
wipe() {
    echo "Wiping emulator..."
    rebble wipe
}

# Function to push the project to the Pebble
push() {
    echo "Pushing project to Pebble..."
    rebble install --phone "$1"
}

# Parse the command line argument
COMMAND=$1
USAGE="Usage: ./r {generate|build|install|debug|wipe|push}"

# Check if a command was provided
if [ -z "$COMMAND" ]; then
    echo "$USAGE"
    exit 1
fi

# Execute the corresponding command
case "$COMMAND" in
    generate)
        generate_tz_list
        ;;
    wipe)
        wipe
        ;;
    build)
        build
        ;;
    install)
        install
        ;;
    debug)
        wipe
        build
        install
        # Stream verbose emulator logs for live debugging
        rebble logs --emulator basalt -v
        ;;
    push)
        if [ -z "$2" ]; then
            echo "Error: Phone argument required for push command"
            echo "Usage: ./r push <phone-ip>"
            exit 1
        fi
        push "$2"
        ;;
    *)
        echo "$USAGE"
        exit 1
        ;;
esac

echo "Command '$COMMAND' completed successfully."
exit 0 
