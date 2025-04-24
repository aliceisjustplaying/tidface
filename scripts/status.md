# Project Status Report: Timezone List Generation (TS Conversion)

**Date:** 2025-04-24

**Goal:** Convert the Python script `generate_airport_tz_list.py` to TypeScript (`generateAirportTzList.ts`), ensuring functional equivalence and correctness in generating the `src/c/airport_tz_list.c` file. Performance optimization was a secondary goal.

**Summary:**

The conversion is largely complete, but debugging focused on achieving correct timezone bucket counts and DST transition timestamps compared to the original Python version and IANA definitions. Performance tuning was attempted but led to correctness issues, prioritizing correctness for now.

**Key Issues Encountered & Resolutions:**

1.  **Initial Performance:** The initial TS script was significantly slower than Python.
    *   **Attempted Fix:** Implemented memoization for `geo-tz` lookups and DST transition calculations. Introduced an optimized `findDstTransitions` in `tzCommon.ts` using day-by-day iteration + binary search for the exact hour.
    *   **Status:** Optimization attempts complicated correctness analysis (see point 5). Performance is currently secondary to correctness.

2.  **Missing Timezone (`America/Noronha` / FEN):** The TS script initially missed the bucket for Fernando de Noronha (IATA: FEN).
    *   **Root Cause:** The `airport-data` package provided an incorrect timezone (`America/Fortaleza`) for FEN.
    *   **Resolution:** Added a manual data fix step in `generateAirportTzList.ts` immediately after loading airport data to force FEN's timezone to `America/Noronha`. Verified with debug logs and external sources (Wikipedia).
    *   **Status:** Resolved. FEN and its corresponding timezone bucket (UTC-2, no DST) are now correctly generated.

3.  **Bucket Count Discrepancy:** Initial comparisons showed TS generating 60 buckets while a specific Python run generated 61.
    *   **Root Cause:** Analysis revealed Python (`zoneinfo`) created a separate bucket for `Africa/Casablanca` (0 -> +1 offset) due to its unique 2025 Ramadan DST schedule, while Luxon (TS) initially grouped it with `Europe/London` (also 0 -> +1 offset). Python also had a bug where it failed to assign FEN to the `America/Noronha` bucket.
    *   **Resolution:** Confirmed the Python FEN assignment was a bug. Ensured TS uses a strict 4-value key (`std_offset`, `dst_offset`, `start_ts`, `end_ts`) for bucketing, which *should* correctly separate Casablanca if Luxon provides distinct transition timestamps.
    *   **Status:** The strict bucketing logic is in place in TS. The final bucket count depends on Luxon's data/logic for 2025 Casablanca DST.

4.  **DST Timestamp Discrepancy:** Systematic differences were observed between Luxon and `zoneinfo` timestamps for DST transitions.
    *   **Root Cause:** Luxon returns the precise UTC timestamp of the offset change instant. `zoneinfo` returns the UTC timestamp corresponding to the *local* "wall clock" time of the transition (e.g., 2 AM local).
    *   **Resolution:** Decided Luxon's approach (UTC instant) is technically more correct according to IANA definitions and prioritizes correctness over matching the previous Python implementation's specific behavior.
    *   **Status:** Using Luxon's default timestamp calculation.

5.  **`tzCommon.ts` Loop Accuracy:** The optimized day+binary search loop introduced for performance produced different results than the simple hour-by-hour loop.
    *   **Observation:** The optimized loop resulted in 58 buckets, merging zones (like Beirut/Chisinau) that the slow loop correctly separated (resulting in 60 buckets). Timestamps also differed slightly.
    *   **Resolution:** Prioritizing verifiable correctness, the decision was made to revert `tzCommon.ts` to the **slower, hour-by-hour iteration** method.
    *   **Status:** The hour-by-hour loop is currently active in `tzCommon.ts`.

**Current State (as of 2025-04-24T18:18:00Z):**

*   `scripts/generateAirportTzList.ts`: Contains the FEN data fix and uses strict 4-value bucketing.
*   `scripts/tzCommon.ts`: Contains the **slow, simple hour-by-hour loop** for `findDstTransitions`.
*   The last run (using the optimized loop) generated `src/c/airport_tz_list.c` with **58 buckets**.
*   The previous run using the *slow* loop generated **60 buckets**, which appeared more correct.

**Next Immediate Step:**

1.  Run `npm run generate:airport` with the current codebase (using the slow loop in `tzCommon.ts`).
2.  Verify the console output reports **60 buckets**.
3.  Inspect the generated `src/c/airport_tz_list.c` to confirm the structure and timestamps match expectations based on the previous reliable "slow loop" run (e.g., check separation of Beirut/Chisinau).

**Future Considerations:**

*   If the ~4-minute runtime of the slow loop becomes unacceptable, revisit the optimized day+binary search in `tzCommon.ts` to identify and fix the cause of the timestamp discrepancies compared to the hour-by-hour method.
*   Consider adding automated tests comparing generated bucket counts and key timestamps against known values for specific years/zones. 
