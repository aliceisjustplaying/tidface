# tz_common.py
"""Common timezone utilities shared by both generators."""
from datetime import datetime, timedelta, timezone
import zoneinfo
from functools import lru_cache


def get_tz_details(tz_name: str, dt_utc: datetime) -> tuple[int, timedelta] | None:
    """Return (offset_seconds, dst_timedelta) or None if the timezone is invalid."""
    try:
        tz = zoneinfo.ZoneInfo(tz_name)
        offset_td = tz.utcoffset(dt_utc)
        dst_td = tz.dst(dt_utc) or timedelta(0)
        if offset_td is not None:
            return int(offset_td.total_seconds()), dst_td
    except Exception:
        pass
    return None


@lru_cache(maxsize=None)
def find_dst_transitions(tz_name: str, year: int) -> tuple[int, int, int, int]:
    """Return (std_offset_sec, dst_offset_sec, dst_start_utc_ts, dst_end_utc_ts).
    If the zone does not observe DST, std == dst and transition timestamps are 0.
    """
    std_offset_sec = None
    dst_offset_sec = None
    start_ts = 0
    end_ts = 0

    # Start one hour before the year to catch boundary transitions
    current_dt = datetime(year, 1, 1, tzinfo=timezone.utc) - timedelta(hours=1)
    initial = get_tz_details(tz_name, current_dt)
    if not initial:
        return 0, 0, 0, 0

    prev_off, prev_dst = initial
    total_hours = (366 * 24) + 3  # cover leap year + buffer

    for _ in range(total_hours):
        current_dt += timedelta(hours=1)
        details = get_tz_details(tz_name, current_dt)
        if not details:
            continue
        cur_off, cur_dst = details

        # Track seen std/dst offsets
        if cur_dst == timedelta(0):
            std_offset_sec = cur_off
        else:
            dst_offset_sec = cur_off

        # Detect DST toggles
        if cur_dst != prev_dst:
            ts = int(current_dt.timestamp())
            if current_dt.year == year:
                if prev_dst == timedelta(0) and cur_dst > timedelta(0):
                    start_ts = ts
                elif prev_dst > timedelta(0) and cur_dst == timedelta(0):
                    end_ts = ts
        prev_off, prev_dst = cur_off, cur_dst

    # Fallback if never set
    if std_offset_sec is None:
        std_offset_sec = prev_off
    if dst_offset_sec is None:
        dst_offset_sec = std_offset_sec

    # If offsets differ by less than 1 minute, treat as no DST
    if abs(std_offset_sec - dst_offset_sec) < 60:
        start_ts = 0
        end_ts = 0
        dst_offset_sec = std_offset_sec

    return std_offset_sec, dst_offset_sec, start_ts, end_ts

# --- Main execution (for CLI testing) ---
if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("Usage: python tz_common.py <timezone_name> <year>", file=sys.stderr)
        sys.exit(1)

    tz_name_arg = sys.argv[1]
    try:
        year_arg = int(sys.argv[2])
    except ValueError:
        print(f"Error: Invalid year '{sys.argv[2]}'", file=sys.stderr)
        sys.exit(1)

    # Ensure zoneinfo is available if needed by get_tz_details
    try:
        import zoneinfo
    except ImportError:
        print("Error: Python 3.9+ with zoneinfo is required to run this script directly.", file=sys.stderr)
        sys.exit(1)

    try:
        result = find_dst_transitions(tz_name_arg, year_arg)
        # Print tuple directly for easy parsing
        print(result)
    except Exception as e:
        print(f"Error processing {tz_name_arg} for {year_arg}: {e}", file=sys.stderr)
        sys.exit(1) 
