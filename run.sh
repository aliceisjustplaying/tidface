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
build_project() {
    echo "Building project..."
    rebble build
}

# Function to install the project on the emulator
install_project() {
    echo "Installing project on emulator..."
    rebble install --emulator basalt
}

# Parse the command line argument
COMMAND=$1

# Check if a command was provided
if [ -z "$COMMAND" ]; then
    echo "Usage: ./run.sh {generate|build|install|debug}"
    exit 1
fi

# Execute the corresponding command
case "$COMMAND" in
    generate)
        generate_tz_list
        ;;
    build)
        build_project
        ;;
    install)
        install_project
        ;;
    debug)
        echo "Running debug (build, install, and tail logs)..."
        build_project
        install_project
        # Stream verbose emulator logs for live debugging
        rebble logs --emulator basalt -v
        ;;
    *)
        echo "Usage: ./run.sh {generate|build|install|debug}"
        exit 1
        ;;
esac

echo "Command '$COMMAND' completed successfully."
exit 0 
