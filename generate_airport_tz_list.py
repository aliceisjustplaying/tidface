#!/usr/bin/env python3
"""Generate C timezone list for the Top-1000 airports.

1. Parse `top1000.html` (downloaded from GetToCenter) to extract IATA codes and
   airport names.
2. Use the `airportsdata` package to obtain the IANA timezone (`tz`) for each
   airport.
3. For each distinct set of (std_offset, dst_offset, dst_start, dst_end)
   belonging to that timezone (for the current year), build a bucket of airport
   IATA codes.
4. Emit a C source file (`src/c/airport_tz_list.c`) that mirrors the structure
   of `tz_list.c` already used by the Closest-Noon clock, but with **airport
   IATA codes** in the pooled name list instead of city names.

Usage:
    # Always parse HTML top1000.html, then fallback for missing offsets
    python generate_airport_tz_list.py --html top1000.html --out src/c/airport_tz_list.c --top 10 --max-bucket 3

Dependencies:
    pip install airportsdata beautifulsoup4 pandas requests

This script intentionally re-implements the DST-transition detection logic from
`generate_tz_list.py` so it can remain self-contained.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from datetime import datetime, timedelta, timezone
from typing import Dict, List, Tuple
from functools import lru_cache

import zoneinfo  # stdlib >=3.9
from bs4 import BeautifulSoup  # type: ignore
import airportsdata  # pip install airportsdata
import pandas as pd  # pip install pandas pyarrow
import requests
import io
import gzip

# ---------------------------------------------------------------------------
# Helper functions (copied & trimmed from generate_tz_list.py)
# ---------------------------------------------------------------------------

def _get_tz_details(tz_name: str, dt_utc: datetime) -> Tuple[int, timedelta] | None:
    """Return (total_offset_seconds, dst_component) or None if tz is invalid."""
    try:
        tz = zoneinfo.ZoneInfo(tz_name)
        off = tz.utcoffset(dt_utc)
        dst = tz.dst(dt_utc) or timedelta(0)
        if off is not None:
            return int(off.total_seconds()), dst
    except Exception:
        pass
    return None

@lru_cache(maxsize=None)
def _find_dst_transitions(tz_name: str, year: int) -> Tuple[int, int, int, int]:
    """Return (std_offset_s, dst_offset_s, dst_start_utc_ts, dst_end_utc_ts).

    If the zone does not observe DST, std == dst and the transition timestamps
    are 0.
    """
    std_offset_sec = None
    dst_offset_sec = None
    start_ts = 0
    end_ts = 0

    # Iterate hour by hour from [year-01-01 00:00-01h] through end of the year
    current_dt = datetime(year, 1, 1, tzinfo=timezone.utc) - timedelta(hours=1)
    initial = _get_tz_details(tz_name, current_dt)
    if not initial:
        return (0, 0, 0, 0)

    prev_off, prev_dst = initial
    total_hours = (366 * 24) + 3  # cover leap + buffer

    for _ in range(total_hours):
        current_dt += timedelta(hours=1)
        details = _get_tz_details(tz_name, current_dt)
        if not details:
            continue
        cur_off, cur_dst = details

        # Track seen std/dst offsets
        if cur_dst == timedelta(0):
            std_offset_sec = cur_off
        else:
            dst_offset_sec = cur_off

        # Detect transition when DST component toggles
        if cur_dst != prev_dst:
            ts = int(current_dt.timestamp())
            if current_dt.year == year:
                if prev_dst == timedelta(0) and cur_dst > timedelta(0):
                    start_ts = ts
                elif prev_dst > timedelta(0) and cur_dst == timedelta(0):
                    end_ts = ts
        prev_off, prev_dst = cur_off, cur_dst

    if std_offset_sec is None:
        std_offset_sec = prev_off
    if dst_offset_sec is None:
        dst_offset_sec = std_offset_sec

    # If offsets differ by <1 min, treat as no DST.
    if abs(std_offset_sec - dst_offset_sec) < 60:
        start_ts = 0
        end_ts = 0
        dst_offset_sec = std_offset_sec

    return (std_offset_sec, dst_offset_sec, start_ts, end_ts)

# ---------------------------------------------------------------------------
# Build ranked list of airports with route counts (fallback if HTML omitted)
# ---------------------------------------------------------------------------

def _download_routes_csv() -> pd.DataFrame:
    """Fetch routes.dat from the OpenFlights repo and return a DataFrame."""
    url = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"
    df = pd.read_csv(url, header=None, usecols=[2, 4], names=["src", "dst"], dtype=str)
    return df

def _rank_airports_by_routes(airport_df: pd.DataFrame) -> pd.Series:
    """Return Series indexed by IATA with descending route hit counts."""
    routes = _download_routes_csv()
    counts = pd.concat([routes["src"], routes["dst"]]).value_counts()
    return counts

def build_topN_per_timezone(top_n: int) -> List[Tuple[str, str]]:
    """Return a balanced list covering all timezones with up to top_n airports each.

    The ranking metric is route_hits (descending).  Airports lacking route data
    default to zero, but they might still be picked to cover empty timezones.
    """
    adict = airportsdata.load("IATA")
    # Build DataFrame and remove record-level 'iata' column to avoid duplicates
    df = pd.DataFrame.from_dict(adict, orient="index")
    if 'iata' in df.columns:
        df = df.drop(columns=['iata'])
    df = df.reset_index().rename(columns={'index': 'iata'})

    # Add route_hits counts
    counts = _rank_airports_by_routes(df)
    df["route_hits"] = df["iata"].map(counts).fillna(0).astype(int)

    # Sort by route_hits descending
    df_sorted = df.sort_values("route_hits", ascending=False, ignore_index=True)

    tz_to_codes: Dict[str, List[Tuple[str, str]]] = {}

    # First pass: iterate sorted df to fill up to top_n per tz
    for _, row in df_sorted.iterrows():
        tz = row["tz"]
        if not isinstance(tz, str) or tz == "":
            continue
        lst = tz_to_codes.setdefault(tz, [])
        if len(lst) < top_n:
            lst.append((row["iata"], row["name"]))
        # Early exit optimisation – if all tz have top_n we can break; but we
        # don't know total tz count easily, so skip.

    # Now build final list
    final_list: List[Tuple[str, str]] = []
    for codes in tz_to_codes.values():
        final_list.extend(codes)
    return final_list

# ---------------------------------------------------------------------------
# Parsing HTML (optional) ---------------------------------------------------
# ---------------------------------------------------------------------------

def _parse_top1000(html_path: Path) -> List[Tuple[str, str]]:
    """Return list of (IATA, Airport Name) found in the HTML table."""
    soup = BeautifulSoup(html_path.read_text(encoding="utf-8"), "html.parser")
    rows = soup.find_all("tr")
    results: List[Tuple[str, str]] = []
    for tr in rows:
        tds = tr.find_all("td")
        if len(tds) < 3:
            continue
        iata = tds[2].get_text(strip=True).upper()
        if not (iata and len(iata) == 3):
            continue  # skip ads rows etc.
        # Airport name usually inside the 2nd <td>, perhaps in an <h2>
        name_cell_text = tds[1].get_text(" ", strip=True)
        results.append((iata, name_cell_text))
    return results

# ---------------------------------------------------------------------------
# Main C-code generation routine
# ---------------------------------------------------------------------------

def generate_c_code(airports_list: List[Tuple[str, str]], out_path: Path, group_size: int = 0, max_bucket: int = 0) -> None:
    """Generate airport_tz_list.c:
    1) Build full buckets for every IATA tz variant (std, dst, transitions).
    2) Pick top group_size codes per std-offset from HTML list.
    3) Fallback for missing offsets (min 1, max max_bucket) using classification + traffic.
    4) Distribute codes evenly across DST buckets, cap each to max_bucket.
    """
    year = datetime.now(timezone.utc).year
    airport_db = airportsdata.load("IATA")
    # Ensure unique HTML airport entries by IATA code
    seen_iatas: set[str] = set()
    unique_airports: List[Tuple[str, str]] = []
    for iata, name in airports_list:
        if iata not in seen_iatas:
            unique_airports.append((iata, name))
            seen_iatas.add(iata)
    airports_list = unique_airports
    # Build fallback DataFrame with classification and traffic for missing offsets
    df_all = pd.DataFrame.from_dict(airport_db, orient="index")
    if 'iata' in df_all.columns:
        df_all = df_all.drop(columns=['iata'])
    df_all = df_all.reset_index().rename(columns={'index': 'iata'})
    # Merge OurAirports classification
    try:
        oa = pd.read_csv("https://ourairports.com/data/airports.csv", usecols=["iata_code","type","scheduled_service"])  # type: ignore
        oa = oa.rename(columns={"iata_code": "iata"}).dropna(subset=["iata"])
        df_all = df_all.merge(oa[['iata','type','scheduled_service']], on='iata', how='left')
    except Exception:
        df_all['type'] = None
        df_all['scheduled_service'] = None
    # Add route hit counts
    traffic_counts = _rank_airports_by_routes(df_all)
    traffic_dict = traffic_counts.to_dict()
    # Map route hits using apply to ensure a Series
    df_all['route_hits'] = df_all['iata'].apply(lambda x: traffic_dict.get(x, 0)).astype(int)
    # Compute standard offset seconds for each record
    df_all['std_offset_s'] = df_all['tz'].apply(lambda tz: _find_dst_transitions(tz, year)[0])

    # Fallback selector for a given std_offset
    def _fallback_codes(std_s: int) -> List[str]:
        """Fallback hierarchy per std offset:
           1) up to max_bucket (or 3) large/international,
           2) up to 2 medium/regional,
           3) up to 1 small_airport,
           4) fill any remaining to reach at least 1, max_bucket total."""
        seg = df_all[df_all['std_offset_s'] == std_s]
        if seg.empty:
            return []
        seg_sorted = seg.sort_values('route_hits', ascending=False)
        result: List[str] = []
        # 1) large_international
        large = seg_sorted[(seg_sorted['type'] == 'large_airport') & (seg_sorted['scheduled_service'] == 'yes')]
        if not large.empty:
            cap = max_bucket if max_bucket > 0 else 3
            result = large['iata'].head(cap).tolist()
        # 2) medium_regional
        remain = (max_bucket - len(result)) if max_bucket > 0 else (3 - len(result))
        if remain > 0:
            medium = seg_sorted[(seg_sorted['type'] == 'medium_airport') & (seg_sorted['scheduled_service'] == 'yes')]
            if not medium.empty:
                mcap = min(remain, 2)
                result.extend(medium['iata'].head(mcap).tolist())
                remain = (max_bucket - len(result)) if max_bucket > 0 else (3 - len(result))
        # 3) small_airport
        if remain > 0:
            small = seg_sorted[(seg_sorted['type'] == 'small_airport') & (seg_sorted['scheduled_service'] == 'yes')]
            if not small.empty:
                result.extend(small['iata'].head(1).tolist())
                remain = (max_bucket - len(result)) if max_bucket > 0 else (3 - len(result))
        # 4) any to ensure at least one
        if not result:
            result = [seg_sorted['iata'].iloc[0]]
        # enforce max_bucket hard limit
        if max_bucket > 0 and len(result) > max_bucket:
            result = result[:max_bucket]
        return result

    # 1) Build full buckets from all tz names in df_all
    full_buckets: Dict[Tuple[int,int,int,int], Dict[str, object]] = {}
    group_keys: Dict[int, List[Tuple[int,int,int,int]]] = {}
    for tz_name in df_all['tz'].dropna().unique():
        std_s, dst_s, start_ts, end_ts = _find_dst_transitions(tz_name, year)
        key = (std_s, dst_s, start_ts, end_ts)
        if key not in full_buckets:
            full_buckets[key] = { 'std': std_s, 'dst': dst_s, 'start': start_ts, 'end': end_ts }
            group_keys.setdefault(std_s, []).append(key)

    # 2) Collect codes from HTML for each std_offset (popular timezones)
    group_codes: Dict[int, List[str]] = {}
    for iata, _ in airports_list:
        rec = airport_db.get(iata)
        if not rec or not rec.get('tz'):
            continue
        std_s = _find_dst_transitions(rec['tz'], year)[0]
        codes = group_codes.setdefault(std_s, [])
        if iata not in codes:
            codes.append(iata)
    # Trim HTML-based codes to group_size for popular timezones
    if group_size > 0:
        for std_s, codes in list(group_codes.items()):
            group_codes[std_s] = codes[:group_size]

    # 3) Fallback for offsets lacking HTML codes (unpopular timezones)
    for std_s, keys in group_keys.items():
        # ensure at least one code per std_offset
        if not group_codes.get(std_s):
            group_codes[std_s] = _fallback_codes(std_s)

    # 4) Assign popular codes to their actual DST buckets, then fallback for empty
    # initialize codes list for each bucket
    for key, meta in full_buckets.items():
        meta['codes'] = []
    # populate HTML-based codes into their real tz variant buckets
    for std_s, codes in group_codes.items():
        for iata in codes:
            rec = airport_db.get(iata)
            if rec and rec.get('tz'):
                std2, dst2, st2, ed2 = _find_dst_transitions(rec['tz'], year)
                bucket_key = (std2, dst2, st2, ed2)
                if bucket_key in full_buckets:
                    full_buckets[bucket_key]['codes'].append(iata)
    # fallback for buckets still empty: only populate the first empty bucket per std-offset
    for std_s, keys in group_keys.items():
        # track HTML-derived codes for this std-offset
        assigned = set(group_codes.get(std_s, []))
        # prepare a one-time fallback candidate list, filtered of already assigned
        fallback_candidates = [c for c in _fallback_codes(std_s) if c not in assigned]
        fallback_used = False
        for bucket_key in keys:
            codes_list = full_buckets[bucket_key].get('codes', [])
            if not codes_list and not fallback_used and fallback_candidates:
                # assign up to max_bucket fallback codes to first empty bucket
                if max_bucket > 0:
                    codes_list = fallback_candidates[:max_bucket]
                else:
                    codes_list = fallback_candidates[:]
                fallback_used = True
            # cap any list to max_bucket if needed
            if codes_list and max_bucket > 0:
                codes_list = codes_list[:max_bucket]
            full_buckets[bucket_key]['codes'] = codes_list
            # record assigned codes so we don't reuse them (though fallback is one-shot)
            assigned.update(codes_list)

    # build ordered bucket list
    buckets_list = [
        full_buckets[k]
        for k in sorted(
            full_buckets.keys(),
            key=lambda k: (full_buckets[k]['std'], full_buckets[k]['dst'], full_buckets[k]['start'])
        )
    ]

    # 5) Build flat pool and offsets
    code_pool = []
    for b in buckets_list:
        b['offset'] = len(code_pool)
        b['count'] = len(b.get('codes', []))
        code_pool.extend(b.get('codes', []))

    # Build name pool parallel to code_pool
    name_pool = []
    for code in code_pool:
        rec = airport_db.get(code)
        if rec and rec.get('name'):
            name = rec['name']
        else:
            name = code
        # Remove ' International Airport' or ' Airport' from the end
        if name.endswith(' International Airport'):
            name = name[:-len(' International Airport')]
        elif name.endswith(' Airport'):
            name = name[:-len(' Airport')]
        name = name.rstrip()
        name_pool.append(name)

    # Emit C file
    with out_path.open("w", encoding="utf-8") as f:
        f.write("// Auto-generated by generate_airport_tz_list.py\n")
        f.write(f"// Year-specific DST data for {year}\n\n")
        f.write("#include <stdint.h>\n\n")
        # Code pool
        f.write("static const char* airport_code_pool[] = {\n")
        for code in code_pool:
            f.write(f"    \"{code}\",\n")
        f.write("};\n\n")

        # Name pool
        f.write("static const char* airport_name_pool[] = {\n")
        for name in name_pool:
            f.write(f"    \"{name}\",\n")
        f.write("};\n\n")

        # Struct matches TzInfo definition
        f.write("typedef struct {\n")
        f.write("    float std_offset_hours;\n")
        f.write("    float dst_offset_hours;\n")
        f.write("    int64_t dst_start_utc;\n")
        f.write("    int64_t dst_end_utc;\n")
        f.write("    int name_offset;\n")
        f.write("    int name_count;\n")
        f.write("} TzInfo;\n\n")

        f.write("static const TzInfo airport_tz_list[] = {\n")
        for bucket in buckets_list:
            std_h = bucket["std"] / 3600.0
            dst_h = bucket["dst"] / 3600.0
            start = bucket["start"]
            end = bucket["end"]
            off = bucket["offset"]
            cnt = bucket["count"]
            f.write(f"    {{ {std_h:.2f}f, {dst_h:.2f}f, {start}LL, {end}LL, {off}, {cnt} }},\n")
        f.write("};\n\n")
        f.write("#define AIRPORT_TZ_LIST_COUNT (sizeof(airport_tz_list)/sizeof(airport_tz_list[0]))\n")
        f.write("#define AIRPORT_CODE_POOL_COUNT (sizeof(airport_code_pool)/sizeof(airport_code_pool[0]))\n")
        f.write("#define AIRPORT_NAME_POOL_COUNT (sizeof(airport_name_pool)/sizeof(airport_name_pool[0]))\n")

    print(
        f"Generated {out_path} with {len(buckets_list)} tz buckets and {len(code_pool)} airports."
    )

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv: List[str] | None = None) -> None:
    parser = argparse.ArgumentParser(
        description="Generate airport_tz_list.c: hybrid grouping by standard offset, split DST buckets, fallback for missing offsets"
    )
    parser.add_argument(
        "--html",
        type=Path,
        default=Path("top1000.html"),
        help="Path to GetToCenter HTML file (top1000.html)",
    )
    parser.add_argument("--out", type=Path, default=Path("src/c/airport_tz_list.c"), help="C output file path")
    parser.add_argument(
        "--top",
        type=int,
        default=10,
        help="Number of airports to pick per standard offset group before splitting across DST buckets",
    )
    parser.add_argument(
        "--max-bucket",
        type=int,
        default=10,
        help="Maximum number of airport codes to include per DST bucket (default: 10)",
    )
    args = parser.parse_args(argv)

    # Always parse the HTML source for list of top airports
    if not args.html.exists():
        print(f"ERROR: HTML file not found: {args.html}", file=sys.stderr)
        sys.exit(1)
    airports_list = _parse_top1000(args.html)
    if not airports_list:
        print(f"ERROR: No airports found in HTML: {args.html}", file=sys.stderr)
        sys.exit(1)

    # group_size = top N per std-offset, max_bucket = cap per DST bucket
    generate_c_code(airports_list, args.out, group_size=args.top, max_bucket=args.max_bucket)


if __name__ == "__main__":
    main() 
