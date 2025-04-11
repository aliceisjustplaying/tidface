# Requires Python 3.9+ for zoneinfo
import zoneinfo
from datetime import datetime, timedelta, timezone
# No unused imports found (time is used for .timestamp())

# Helper to get offset and DST component safely
def get_tz_details(tz_name: str, dt_utc: datetime) -> tuple[int, timedelta] | None:
    """Gets total offset in seconds and DST component as timedelta."""
    try:
        tz = zoneinfo.ZoneInfo(tz_name)
        offset_td = tz.utcoffset(dt_utc)
        dst_td = tz.dst(dt_utc)

        # Ensure DST is not None, default to zero if it is (e.g., for UTC)
        if dst_td is None:
            dst_td = timedelta(0)

        if offset_td is not None:
            return int(offset_td.total_seconds()), dst_td
        # If offset_td is None, implicitly returns None below
    except Exception:
        # print(f"Warning: Could not get details for {tz_name} at {dt_utc}: {e}")
        pass # Silently ignore errors for individual lookups
    return None

# Function to find DST transitions within a year
def find_dst_transitions_accurate(tz_name: str, year: int) -> tuple[int, int, int, int]:
    """ Finds precise DST transition UTC timestamps for a given year.
        Returns (std_offset_sec, dst_offset_sec, last_start_utc_ts, last_end_utc_ts)
        Timestamps are UTC seconds (epoch). 0 if no transition/no DST found in the year.
    """
    start_ts = 0
    end_ts = 0
    std_offset_sec = None
    dst_offset_sec = None # Offset *during* DST
    initial_offset_sec = None # Store the very first valid offset

    try:
        # Start iterating from one hour before the target year begins
        # Ensures transitions exactly at year start are caught
        current_dt = datetime(year , 1, 1, 0, 0, 0, tzinfo=timezone.utc) - timedelta(hours=1)
        initial_details = get_tz_details(tz_name, current_dt)

        if not initial_details:
             # Fallback if start fails: try noon on Jan 1st
             current_dt = datetime(year , 1, 1, 12, 0, 0, tzinfo=timezone.utc)
             initial_details = get_tz_details(tz_name, current_dt)
             if not initial_details:
                 print(f"Warning: Cannot get initial offset for {tz_name} in {year}")
                 return 0, 0, 0, 0

        prev_offset_sec, prev_dst_td = initial_details
        initial_offset_sec = prev_offset_sec # Store the first offset we found

        # Iterate hour by hour through the target year plus a few hours into the next
        total_hours_to_check = (366 * 24) + 3 # Cover leap year + buffer

        for _ in range(total_hours_to_check):
            current_dt += timedelta(hours=1)
            details = get_tz_details(tz_name, current_dt)

            if not details: continue # Skip if data unavailable for this hour

            current_offset_sec, current_dst_td = details

            # --- Determine Standard vs DST offset ---
            # Continuously update std/dst based on whether DST is active
            if current_dst_td == timedelta(0):
                std_offset_sec = current_offset_sec
            if current_dst_td > timedelta(0):
                dst_offset_sec = current_offset_sec

            # --- Detect transition based on change in DST component ---
            if prev_dst_td != current_dst_td:
                transition_ts = int(current_dt.timestamp())

                # Record transition timestamp if it happens *within* the target year
                if current_dt.year == year:
                    if prev_dst_td == timedelta(0) and current_dst_td > timedelta(0):
                        # Entered DST (Std -> Dst)
                        start_ts = transition_ts
                        # Ensure offsets are recorded based on this transition
                        if std_offset_sec is None: std_offset_sec = prev_offset_sec
                        if dst_offset_sec is None: dst_offset_sec = current_offset_sec

                    elif prev_dst_td > timedelta(0) and current_dst_td == timedelta(0):
                        # Exited DST (Dst -> Std)
                        end_ts = transition_ts
                        # Ensure offsets are recorded based on this transition
                        if std_offset_sec is None: std_offset_sec = current_offset_sec
                        if dst_offset_sec is None: dst_offset_sec = prev_offset_sec

            prev_offset_sec, prev_dst_td = current_offset_sec, current_dst_td

        # --- Post-processing ---
        # Use the very first offset seen if std/dst couldn't be determined otherwise
        if std_offset_sec is None: std_offset_sec = initial_offset_sec
        if dst_offset_sec is None: dst_offset_sec = std_offset_sec # Default DST offset to STD if not seen

        # If offsets are effectively the same, clear transition timestamps
        OFFSET_DIFF_THRESHOLD_SECONDS = 60 # Use a named constant
        if abs(std_offset_sec - dst_offset_sec) < OFFSET_DIFF_THRESHOLD_SECONDS:
             start_ts = 0
             end_ts = 0
             dst_offset_sec = std_offset_sec # Ensure they are identical if no DST

    except zoneinfo.ZoneInfoNotFoundError:
        print(f"Warning: Timezone '{tz_name}' not found during transition check.")
        return 0, 0, 0, 0
    except Exception as e:
        print(f"Error finding transitions for {tz_name}: {e}")
        return 0, 0, 0, 0

    # Ensure we return non-None values
    std_offset_sec = std_offset_sec if std_offset_sec is not None else 0
    dst_offset_sec = dst_offset_sec if dst_offset_sec is not None else 0

    return std_offset_sec, dst_offset_sec, start_ts, end_ts

def generate_tz_list_c_code():
    """Generates C code for a static timezone list with DST transition timestamps."""

    target_year = datetime.now().year # Use current year for transitions
    print(f"Finding DST transitions for year {target_year}...")

    available_zones = zoneinfo.available_timezones()
    print(f"Found {len(available_zones)} available timezones.")

    processed_zones = {} # Key: TUPLE(std_offset_s, dst_offset_s, start_utc, end_utc), Value: Dict of zone data

    for tz_name in available_zones:
        # Basic filtering (no dead code here)
        if tz_name.startswith("Etc/") or "/" not in tz_name: continue
        if tz_name in ["Factory", "factory"] or tz_name.lower().startswith("right/") or tz_name.lower().startswith("posix/"): continue

        std_offset_s, dst_offset_s, start_utc, end_utc = find_dst_transitions_accurate(tz_name, target_year)
        city_name = tz_name.split('/')[-1].replace('_', ' ')

        # --- Filter out generic names ---
        # Comprehensive list based on review of tz_list.c
        generic_names_to_exclude = {
            "Samoa", "Hawaii", "Aleutian", "Alaska", "Pacific", "Arizona", "Yukon",
            "Mountain", "General", "Saskatchewan", "Central", "Knox IN", "EasterIsland",
            "Acre", "Jamaica", "Michigan", "Eastern", "East-Indiana", "Atlantic",
            "Continental", "Newfoundland", "East", "Bahia", "Noronha", "South Georgia",
            "Canary", "Faeroe", "Faroe", "Guernsey", "Isle of Man", "Jersey",
            "Madeira", "Jan Mayen", "West", "North", "South", "ACT", "NSW",
            "Tasmania", "Victoria", "Queensland", "Yap", "South Pole", "Kanton",
            # Add or remove names as needed
        }
        # Case-insensitive check for exclusion
        if city_name.lower() in {name.lower() for name in generic_names_to_exclude}:
            continue # Skip this generic name

        # Convert offsets back to hours for potential display, but keep seconds for key
        std_offset_h = std_offset_s / 3600.0
        dst_offset_h = dst_offset_s / 3600.0

        # Group by the unique combination of std offset, dst offset, and transitions
        key_tuple = (std_offset_s, dst_offset_s, start_utc, end_utc)
        if city_name and city_name[0].isupper():
            if key_tuple not in processed_zones:
                 processed_zones[key_tuple] = {
                    "std_offset_s": std_offset_s, # Store seconds internally
                    "dst_offset_s": dst_offset_s,
                    "start_utc": start_utc,
                    "end_utc": end_utc,
                    "names": []
                 }
            # Add city name if not already present
            if city_name not in processed_zones[key_tuple]["names"]:
                processed_zones[key_tuple]["names"].append(city_name)

    # Convert dict values to a list and sort primarily by standard offset, then DST offset
    # Sort key uses the seconds offset stored in the dictionary value
    tz_data_list = sorted(processed_zones.values(), key=lambda x: (x["std_offset_s"], x["dst_offset_s"]))
    print(f"Generated data for {len(tz_data_list)} unique offset/DST rule combinations.")

    # --- C Code Generation ---
    # Build C code string (no obvious dead code here)
    c_code = "// Generated by Python script using zoneinfo\n"
    c_code += f"// Includes Standard & DST offsets and UTC transition timestamps for {target_year}.\n"
    c_code += "// WARNING: DST rules accurate only for the generated year.\n\n"
    # Include stdint.h for int64_t type used below
    c_code += "#include <stdint.h>\n\n"
    c_code += "// Holds a single city name string\n"
    c_code += "typedef struct {\n"
    c_code += "    const char* name;\n"
    c_code += "} TzCityName;\n\n"
    c_code += "// Holds offset info and points to an array of names\n"
    c_code += "typedef struct {\n"
    c_code += "    float std_offset_hours; // Offset during standard time\n"
    c_code += "    float dst_offset_hours; // Offset during daylight time (if applicable)\n"
    # Using int64_t for Pebble time_t safety, check SDK if 32-bit preferred
    c_code += "    int64_t dst_start_utc;  // UTC timestamp (time_t) for DST start (0 if N/A)\n"
    c_code += "    int64_t dst_end_utc;    // UTC timestamp (time_t) for DST end (0 if N/A)\n"
    c_code += "    const TzCityName* names; // Pointer to the array of names\n"
    c_code += "    int name_count;         // How many names are in the array\n"
    c_code += "} TzInfo;\n\n"

    # Generate the static arrays of names first
    name_array_definitions = ""
    unique_names_id = 0
    for zone_data in tz_data_list:
        c_array_name = f"tz_names_{unique_names_id}"
        zone_data["c_array_name"] = c_array_name # Store for later use
        unique_names_id += 1
        name_array_definitions += f"static const TzCityName {c_array_name}[] = {{\n"
        # Sort names alphabetically for consistency
        for name in sorted(zone_data["names"]):
            name_array_definitions += f"    {{ \"{name}\" }},\n"
        name_array_definitions += "};\n\n"
    c_code += name_array_definitions

    # Generate the main tz_list array
    c_code += "// Main list mapping offsets/DST info to their respective name arrays\n"
    c_code += "static const TzInfo tz_list[] = {\n"
    for zone_data in tz_data_list:
        # Convert offsets to hours ONLY for C code generation
        std_offset_h_c = zone_data['std_offset_s'] / 3600.0
        dst_offset_h_c = zone_data['dst_offset_s'] / 3600.0
        c_array_name = zone_data["c_array_name"]
        name_count = len(zone_data["names"])
        # Use 'LL' suffix for int64_t timestamp constants in C
        c_code += (f"    {{ {std_offset_h_c:.2f}f, {dst_offset_h_c:.2f}f, "
                   f"{zone_data['start_utc']}LL, {zone_data['end_utc']}LL, "
                   f"{c_array_name}, {name_count} }},\n")

    c_code += "};\n\n"
    c_code += f"#define TZ_LIST_COUNT {len(tz_data_list)}\n"

    return c_code

# --- Main execution ---
if __name__ == "__main__":
    c_code_output = generate_tz_list_c_code()
    output_filename = "src/c/tz_list.c" # Output path
    try:
        with open(output_filename, "w") as f:
            f.write(c_code_output)
        print(f"\nSuccessfully written timezone data (with accurate DST timestamps) to {output_filename}")
    except IOError as e:
        print(f"\nError: Could not write to file {output_filename}: {e}")
