# TidFace Clock Logic: Closest to Noon (with DST and Randomization)

## Core Goal

Display a continuously updating time in the format `[CityName]:[MM]:[SS]`. The chosen `CityName` is determined by finding the timezone whose *current local time* is closest to 12:00:00 noon at specific intervals.

## Key Principles

1.  **Base Time:** The system operates based on the current Coordinated Universal Time (UTC), also referred to as GMT.
2.  **Re-evaluation Intervals:** The process of choosing the "best" timezone/city occurs precisely when the UTC time hits **:00, :15, or :30** minutes past the hour.
    *   *Reasoning:* We evaluate at these specific intervals to catch timezones hitting exactly 12:00:00 local time.
        *   `:00` UTC is needed for integer offsets (e.g., UTC+5 -> 12:00 at 07:00 UTC).
        *   `:15` UTC is needed for `:45` offsets (e.g., UTC+5:45 -> 12:00 at 06:15 UTC; UTC+12:45 -> 12:00 at 23:15 UTC).
        *   `:30` UTC is needed for `:30` offsets (e.g., UTC+5:30 -> 12:00 at 06:30 UTC).
    *   The `:45` UTC evaluation is omitted as no standard timezones have `:15` offsets that would hit noon precisely at XX:45 UTC.
3.  **DST Awareness:** Daylight Saving Time rules, as defined for the year 2025 in `tz_list.c`, *must* be considered when calculating local times.
4.  **Randomization:** If multiple timezones are equally "close" to noon, one is chosen randomly. Subsequently, a city name within that chosen timezone is selected randomly.
5.  **Continuous Display:** Between re-evaluation intervals, the displayed time (`MM:SS`) simply ticks forward second-by-second based on the *currently selected* city's timezone offset. The city name remains constant during this period.
6.  **Allowed Time Jumps:** When a new city/timezone is selected at a re-evaluation interval, the displayed `MM:SS` will jump to reflect the *new* local time. This can result in the displayed minutes/seconds appearing to go backward or jump forward significantly.

## Re-evaluation Logic (Executed at UTC XX:00:00, XX:15:00, XX:30:00)

1.  **Get Current UTC:** Obtain the current UTC time as a timestamp (`current_utc_t`, e.g., a `time_t`).
2.  **Define Noon:** Define the target time as 12:00:00 (represented as seconds past midnight, e.g., `NOON_SECONDS = 12 * 3600`).
3.  **Iterate Through Timezones (`tz_list`) - Find Minimum Valid Time:**
    *   Initialize `min_valid_local_seconds` to a very large value (e.g., `DAY_SECONDS`).
    *   Create a temporary list/array (`candidates`) to store potential winners.
    *   For each `TzInfo` entry in `tz_list`:
        *   **Determine Active Offset:** Check if `current_utc_t` falls within the `dst_start_utc` and `dst_end_utc` for that entry. Use `dst_offset_hours` if true, `std_offset_hours` otherwise.
        *   **Calculate Local Time:** Determine the local time for this timezone at `current_utc_t`, expressed as seconds past local midnight (`local_seconds_today`). Ensure the result is correctly handled (e.g., modulo `DAY_SECONDS`, handle negative results).
        *   **Filter for Validity:** Check if `local_seconds_today >= NOON_SECONDS`.
        *   **If Valid:**
            *   Store the `TzInfo` index, `local_seconds_today`, and the `active_offset_hours` in the `candidates` list.
            *   Compare `local_seconds_today` with `min_valid_local_seconds`. If it's smaller, update `min_valid_local_seconds`.
4.  **Collect Best Candidates:**
    *   Create a new list (`best_candidates`).
    *   Iterate through the `candidates` list gathered in step 3.
    *   If a candidate's `local_seconds_today` equals `min_valid_local_seconds`, add its index (or a reference to it) to the `best_candidates` list.
5.  **Handle No Valid Candidates:** If `best_candidates` is empty (meaning no timezone had local time >= noon), set a fallback state (e.g., `selected_city_name = "Wait..."`, `selected_offset_hours = 0.0`).
6.  **Select Winning Timezone (if candidates exist):** Randomly choose one entry from the `best_candidates` list. This gives you the winning candidate structure (containing the `TzInfo` index and active offset).
7.  **Select Winning City:** Get the `TzInfo` entry using the index from the winning candidate. Randomly choose a city name (`selected_city_name`) from the `names` array within that `TzInfo`.
8.  **Store Results:** Store the `selected_city_name` and the `active_offset_hours` from the winning candidate (`selected_offset_hours`). These will be used for display until the next re-evaluation. If a fallback state was set in step 5, store those values instead.

## Continuous Display Logic (Executed Every Second)

1.  **Get Current UTC:** Obtain the current UTC timestamp (`current_utc_t`).
2.  **Calculate Current Local Time:** Use the *stored* `selected_offset_hours` from the last re-evaluation.
    `current_local_time = current_utc_t + (selected_offset_hours * 3600)`
3.  **Format Time:** Extract the minutes (`MM`) and seconds (`SS`) from `current_local_time`.
4.  **Display:** Output the string `selected_city_name:MM:SS`.
