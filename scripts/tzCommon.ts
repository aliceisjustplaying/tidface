import { DateTime } from 'luxon';
import { IANAZone } from 'luxon';

/**
 * Cache for findDstTransitions results. Key: "zoneName:year"
 */
const transitionCache = new Map<string, DstTransitions>();

/**
 * Type definition for the return value of findDstTransitions.
 */
export type DstTransitions = [
  stdOffsetSeconds: number,
  dstOffsetSeconds: number,
  dstStartUtcTimestamp: number,
  dstEndUtcTimestamp: number,
];

/**
 * Represents the details of a timezone at a specific moment.
 */
interface TzDetails {
  offsetSeconds: number;
  isDST: boolean;
}

/**
 * Return (offset_seconds, isDST) or undefined if the timezone is invalid or offset is null.
 */
export function getTzDetails(zoneName: string, dt: DateTime): TzDetails | undefined {
  if (!IANAZone.isValidZone(zoneName)) {
    return undefined;
  }
  // Ensure the input DateTime is in UTC before setting the zone
  const dtInZone = dt.setZone(zoneName);

  if (!dtInZone.isValid) {
    // Failed to apply the zone
    console.warn(`[tzCommon] Failed to set zone ${zoneName} for datetime ${dt.toISO()}`);
    return undefined;
  }

  const offsetMinutes = dtInZone.offset;
  const isDST = dtInZone.isInDST;

  // offset can be null if invalid, checked by dtInZone.isValid
  const offsetSeconds = offsetMinutes * 60;
  return { offsetSeconds, isDST };
}

/**
 * Return [std_offset_sec, dst_offset_sec, dst_start_utc_ts, dst_end_utc_ts].
 * If the zone does not observe DST, std == dst and transition timestamps are 0.
 * Caches results per zone/year combination.
 */
export function findDstTransitions(zoneName: string, year: number): DstTransitions {
  const cacheKey = `${zoneName}:${year}`;
  if (transitionCache.has(cacheKey)) {
    return transitionCache.get(cacheKey)!;
  }

  // Validate zone early
  if (!IANAZone.isValidZone(zoneName)) {
    return [0, 0, 0, 0];
  }

  /*
   * Optimised algorithm
   * -------------------
   * 1. Walk through the year in STEP_HOURS chunks (default 6h) instead of hour-by-hour.
   * 2. When we detect a DST state change between two checkpoints we perform a binary
   *    search down to *hour* granularity to locate the transition start hour.  This
   *    guarantees the **same output** as the previous naive hour-by-hour loop while
   *    reducing the worst-case iterations by ~6× (1460 vs 8760 for a non-leap year).
   * 3. We collect the first STD→DST transition (startTs) and the first DST→STD
   *    transition (endTs) that occur **inside the target year**.
   */

  const STEP_HOURS = 6; // coarse step, must be power-of-two divisor of 24.
  const startOfYear = DateTime.utc(year, 1, 1);
  const startCursor = startOfYear.minus({ hours: 1 }); // one hour before the year
  const endBoundary = DateTime.utc(year + 1, 1, 1).plus({ hours: 2 }); // small buffer

  let cursor = startCursor;
  const firstDetails = getTzDetails(zoneName, cursor);
  if (!firstDetails) {
    // Should never happen for valid zones but keep behaviour consistent.
    return [0, 0, 0, 0];
  }

  let prevIsDST = firstDetails.isDST;
  let prevOffsetSec = firstDetails.offsetSeconds;

  // Track discovered offsets
  let stdOffsetSec: number | null = prevIsDST ? null : prevOffsetSec;
  let dstOffsetSec: number | null = prevIsDST ? prevOffsetSec : null;

  let startTs = 0;
  let endTs = 0;

  // Helper: binary search between two datetimes (inclusive lower, exclusive upper)
  // to find the *first* hour whose DST flag differs from the lower bound.
  const refineTransitionHour = (
    lower: DateTime,
    upper: DateTime,
    lowerIsDST: boolean,
  ): DateTime => {
    // Ensure bounds differ in DST state
    let low = lower;
    let high = upper;
    while (high.diff(low, 'hours').hours > 1) {
      const mid = low.plus({ milliseconds: high.diff(low).as('milliseconds') / 2 });
      const midDetails = getTzDetails(zoneName, mid) as TzDetails; // assume valid
      if (midDetails.isDST === lowerIsDST) {
        low = mid;
      } else {
        high = mid;
      }
    }
    // `high` is now within the first hour of the new DST state
    return high.startOf('hour');
  };

  // Coarse walk across the year
  while (cursor <= endBoundary) {
    const nextCursor = cursor.plus({ hours: STEP_HOURS });
    const details = getTzDetails(zoneName, nextCursor);
    if (!details) {
      // On theoretically invalid zones continue (should not happen)
      cursor = nextCursor;
      continue;
    }

    const curIsDST = details.isDST;
    const curOffsetSec = details.offsetSeconds;

    // Track offsets
    if (curIsDST) {
      dstOffsetSec = curOffsetSec;
    } else {
      stdOffsetSec = curOffsetSec;
    }

    // Detect toggle between prev and current checkpoints
    if (curIsDST !== prevIsDST) {
      // Refine to hour-precision between cursor and nextCursor
      const transitionHour = refineTransitionHour(cursor, nextCursor, prevIsDST);
      const transitionTs = Math.floor(transitionHour.toMillis() / 1000);

      // Determine direction and assign start/end if the transition sits in target year
      if (!prevIsDST && curIsDST) {
        if (transitionHour.year === year) {
          startTs = transitionTs; // first second OF new hour when DST begins
        }
      } else if (prevIsDST && !curIsDST) {
        if (transitionHour.year === year) {
          endTs = transitionTs; // first second OF new hour when DST ends
        }
      }

      // Update prevIsDST to new state for subsequent iterations
      prevIsDST = curIsDST;
      prevOffsetSec = curOffsetSec;

      // Move cursor forward to just after the transition to avoid re-detecting same one
      cursor = transitionHour.plus({ hours: 1 });
      continue;
    }

    // No toggle, advance cursor
    prevIsDST = curIsDST;
    prevOffsetSec = curOffsetSec;
    cursor = nextCursor;
  }

  // Final fallbacks – replicate slow loop behaviour
  if (stdOffsetSec === null && dstOffsetSec !== null) {
    stdOffsetSec = dstOffsetSec;
  }
  if (dstOffsetSec === null && stdOffsetSec !== null) {
    dstOffsetSec = stdOffsetSec;
  }
  if (stdOffsetSec === null) {
    stdOffsetSec = prevOffsetSec;
  }
  if (dstOffsetSec === null) {
    dstOffsetSec = stdOffsetSec;
  }

  // Treat zones with <60-second difference as non-DST
  if (Math.abs(stdOffsetSec - dstOffsetSec) < 60) {
    dstOffsetSec = stdOffsetSec;
    startTs = 0;
    endTs = 0;
  }

  const result: DstTransitions = [stdOffsetSec, dstOffsetSec, startTs, endTs];
  transitionCache.set(cacheKey, result);
  return result;
} 
