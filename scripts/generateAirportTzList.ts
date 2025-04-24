// TODO: Convert generate_airport_tz_list.py to TypeScript 

import { program } from 'commander';
import * as fs from 'fs/promises';
import * as path from 'path';
import * as cheerio from 'cheerio';
// Use require for airport-data as it lacks types
const airports = require('airport-data');
import { find as findTz } from 'geo-tz'; // Use geo-tz instead of timezonefinder
// Remove node-fetch import, use global fetch
import { parse } from 'csv-parse'; // Import csv-parse
import { findDstTransitions, DstTransitions } from './tzCommon';
import {
  findTzCache,
  memoizedFindTz,
  findDstTransitionsCache,
  memoizedFindDstTransitions,
  parseTopHtml,
  downloadRoutesCsv,
  rankAirportsByRoutes,
  downloadOurAirportsCsv,
  getBucketKey,
  AirportInfo,
  OurAirportInfo,
  TzBucketData,
  RouteRecord,
} from './generateAirportTzListHelpers';

// Local-only type used before helper split
interface AirportDataEntry {
  iata?: string;
  name?: string;
  city?: string;
  country?: string;
  latitude?: number | string;
  longitude?: number | string;
  tz?: string;
  type?: string;
  source?: string;
}

// Define interfaces for data structures
interface TzBucketKey {
    stdOffsetSeconds: number;
    dstOffsetSeconds: number;
    dstStartTimestamp: number;
    dstEndTimestamp: number;
}

// Helper function to create bucket keys
// function getBucketKey(details: DstTransitions): string {
//     return `${details[0]}_${details[1]}_${details[2]}_${details[3]}`;
// }

// Placeholder functions matching Python script structure

// async function parseTopHtml(htmlPath: string): Promise<Array<[string, string]>> {
//     console.log(`Parsing HTML file: ${htmlPath}`);
//     const htmlContent = await fs.readFile(htmlPath, 'utf-8');
//     const $ = cheerio.load(htmlContent);
//     const results: Array<[string, string]> = [];
//     $('tr').each((_, tr) => {
//         const tds = $(tr).find('td');
//         if (tds.length < 3) return;
//         const iata = $(tds[2]).text().trim().toUpperCase();
//         if (!iata || iata.length !== 3) return; // Basic validation
//         // Improve name extraction - handle potential h2 tags etc.
//         let name = $(tds[1]).find('h2').first().text().trim();
//         if (!name) {
//             name = $(tds[1]).text().trim();
//         }
//         // Clean up common suffixes (like in Python)
//         if (name.endsWith(' International Airport')) {
//             name = name.substring(0, name.length - ' International Airport'.length);
//         } else if (name.endsWith(' Airport')) {
//             name = name.substring(0, name.length - ' Airport'.length);
//         }
//         results.push([iata, name.trim()]);
//     });
//     console.log(`Found ${results.length} airports in HTML.`);
//     return results;
// }

async function generateCCode(
    airportsList: Array<[string, string]>,
    outPath: string,
    groupSize: number,
    maxBucket: number
): Promise<void> {
    console.log(`Generating C code for ${outPath}...`);
    console.log(`Group size: ${groupSize}, Max bucket size: ${maxBucket}`);

    const year = new Date().getUTCFullYear();

    // Load airport data using require
    const airportDataArray: AirportDataEntry[] = airports as any[];
    const initialAirportDb = new Map<string, AirportInfo>();

    for (const airport of airportDataArray) {
        if (airport.iata) {
            const iataUpper = airport.iata.toUpperCase();
            const lat = typeof airport.latitude === 'number' ? airport.latitude : parseFloat(airport.latitude ?? 'NaN');
            const lon = typeof airport.longitude === 'number' ? airport.longitude : parseFloat(airport.longitude ?? 'NaN');

            if (isNaN(lat) || isNaN(lon) || !airport.name || !airport.city || !airport.country) {
                continue;
            }

            initialAirportDb.set(iataUpper, {
                iata: iataUpper,
                name: airport.name,
                city: airport.city,
                country: airport.country,
                latitude: lat,
                longitude: lon,
                tz: airport.tz || '',
                correctedTz: '',
                type: airport.type,
                source: airport.source,
                scheduled_service: undefined,
                route_hits: 0,
            });
        }
    }
    console.log(`Loaded ${initialAirportDb.size} initial airports.`);

    // Fix known incorrect TZs from airport-data
    const fenAirport = initialAirportDb.get('FEN');
    if (fenAirport && fenAirport.tz !== 'America/Noronha') {
        console.log('[DATA_FIX] Correcting FEN timezone from', fenAirport.tz, 'to America/Noronha');
        fenAirport.tz = 'America/Noronha';
    }

    // --- Fetch/Parse/Rank Routes ---
    console.log('Processing routes...');
    let routeCounts = new Map<string, number>();
    try {
        const routes = await downloadRoutesCsv();
        routeCounts = await rankAirportsByRoutes(routes);
        for (const [iata, count] of routeCounts.entries()) {
            const airportInfo = initialAirportDb.get(iata);
            if (airportInfo) {
                airportInfo.route_hits = count;
            }
        }
        console.log('Integrated route counts.');
    } catch (error) {
        console.warn('Could not get route counts, proceeding without them.');
    }

    // --- Fetch/Parse OurAirports ---
    const ourAirportsData = await downloadOurAirportsCsv();
    for (const [iata, oaInfo] of ourAirportsData.entries()) {
        const airportInfo = initialAirportDb.get(iata);
        if (airportInfo) {
            airportInfo.type = oaInfo.type; // Overwrite type if available
            airportInfo.scheduled_service = oaInfo.scheduled_service;
        }
    }
    if (ourAirportsData.size > 0) {
        console.log('Integrated OurAirports classification data.');
    }

    // --- Correct timezones and build initial buckets ---
    console.log('Correcting timezones and building initial buckets...');
    findTzCache.clear();
    findDstTransitionsCache.clear();
    const airportDb = new Map<string, AirportInfo>();
    const tzBuckets = new Map<string, TzBucketData>();
    const groupKeys = new Map<number, string[]>(); // Map: std_offset_s -> [bucketKey, ...]

    let foundFen = false;
    for (const airport of initialAirportDb.values()) {
        const isFen = airport.iata === 'FEN';
        if (isFen) console.log(`[NORONHA_DEBUG] Processing FEN airport record.`);
        let correctedTz: string | undefined | null = airport.tz;
        // Only fall back to geo-tz if the original tz is empty/missing
        if (!correctedTz) {
            if (isFen) console.log(`[NORONHA_DEBUG] FEN had no original TZ, looking up via geo-tz.`);
            try {
                const foundTzs = memoizedFindTz(airport.latitude, airport.longitude);
                if (foundTzs && foundTzs.length > 0) {
                    correctedTz = foundTzs[0];
                } else {
                    continue; // Skip if no TZ can be determined
                }
            } catch {
                continue; // Skip airports that still fail TZ lookup
            }
        }

        if (isFen) console.log(`[NORONHA_DEBUG] FEN correctedTz: ${correctedTz}`);
        if (correctedTz === 'America/Noronha') foundFen = true; // Track if we process this TZ

        // Update airport info with corrected TZ
        const updatedAirportInfo = { ...airport, correctedTz };
        airportDb.set(updatedAirportInfo.iata, updatedAirportInfo);

        // Get DST details for the corrected timezone
        let dstDetails: DstTransitions | null = null;
        try {
            // *** Use memoized version ***
            dstDetails = memoizedFindDstTransitions(correctedTz, year);
        } catch (e) {
            if (isFen) console.error(`[NORONHA_DEBUG] Error getting DST for FEN/Noronha: ${e}`);
            // This catch block is less likely needed now due to memoized wrapper handling errors
            // console.warn(`Could not get DST transitions for ${correctedTz} (Airport: ${airport.iata}): ${e}`);
            continue;
        }

        if (isFen) console.log(`[NORONHA_DEBUG] FEN dstDetails: ${JSON.stringify(dstDetails)}`);

        if (!dstDetails) continue; // Skip if memoized function returned null (error or invalid TZ)

        // --- Bucket creation/update logic --- (Restored)
        const key = getBucketKey(dstDetails);
        const stdOffset = dstDetails[0];

        if (!tzBuckets.has(key)) {
            tzBuckets.set(key, {
                std: stdOffset,
                dst: dstDetails[1],
                start: dstDetails[2],
                end: dstDetails[3],
                tzNames: new Set([correctedTz]),
                codes: [],
            });
            const keysForStdOffset = groupKeys.get(stdOffset) || [];
            keysForStdOffset.push(key);
            groupKeys.set(stdOffset, keysForStdOffset);
        } else {
            tzBuckets.get(key)!.tzNames.add(correctedTz);
        }
        // --- End bucket logic ---
    }
    console.log(`Finished correcting timezones. Cache sizes: findTz=${findTzCache.size}, findDst=${findDstTransitionsCache.size}`);
    console.log(`[NORONHA_DEBUG] Did we process America/Noronha TZ? ${foundFen}`);

    // Pre-group airports by standard offset for fallback optimization
    console.log('Pre-grouping airports by standard offset...');
    const airportsByStdOffset = new Map<number, AirportInfo[]>();
    for (const airportInfo of airportDb.values()) {
        if (!airportInfo.correctedTz) continue;
        try {
            const dstDetails = memoizedFindDstTransitions(airportInfo.correctedTz, year);
            if (dstDetails) {
                const stdOffset = dstDetails[0];
                const list = airportsByStdOffset.get(stdOffset) || [];
                list.push(airportInfo);
                airportsByStdOffset.set(stdOffset, list);
            }
        } catch {
            // Ignore errors here, airport just won't be in the pre-grouped map
        }
    }
    console.log(`Finished pre-grouping into ${airportsByStdOffset.size} standard offset groups.`);

    // Check if the Noronha bucket exists after grouping
    const noronhaKey = `${-7200}_${-7200}_${0}_${0}`; // Expected key: std=-2h, dst=-2h, start=0, end=0
    console.log(`[NORONHA_DEBUG] Does Noronha bucket key (${noronhaKey}) exist in tzBuckets? ${tzBuckets.has(noronhaKey)}`);

    // Determine fallback codes
    const getFallbackCodes = (stdOffsetSeconds: number): string[] => {
        // Retrieve pre-filtered candidates for this standard offset
        const initialCandidates = airportsByStdOffset.get(stdOffsetSeconds) || [];

        if (initialCandidates.length === 0) return [];

        // Sort the much smaller candidate list
        const candidates = initialCandidates.sort((a, b) => b.route_hits - a.route_hits); // Sort by route hits desc

        if (candidates.length === 0) return [];

        const result: string[] = [];
        const scheduledYes = (a: AirportInfo) => a.scheduled_service === 'yes';

        // Tier 1: Large, scheduled
        const large = candidates.filter(a => a.type === 'large_airport' && scheduledYes(a));
        result.push(...large.slice(0, maxBucket > 0 ? maxBucket : 3).map(a => a.iata));

        // Tier 2: Medium, scheduled
        let remain = (maxBucket > 0 ? maxBucket : 3) - result.length;
        if (remain > 0) {
            const medium = candidates.filter(a => a.type === 'medium_airport' && scheduledYes(a) && !result.includes(a.iata));
            result.push(...medium.slice(0, Math.min(remain, 2)).map(a => a.iata));
        }

        // Tier 3: Small, scheduled
        remain = (maxBucket > 0 ? maxBucket : 3) - result.length;
        if (remain > 0) {
            const small = candidates.filter(a => a.type === 'small_airport' && scheduledYes(a) && !result.includes(a.iata));
            result.push(...small.slice(0, 1).map(a => a.iata));
        }

        // Tier 4: Ensure at least one if possible
        if (result.length === 0) {
            result.push(candidates[0].iata); // Add the top-ranked overall
        }

        // Enforce max bucket size strictly if needed (though applied later too)
        return maxBucket > 0 ? result.slice(0, maxBucket) : result;
    };

    // --- Group codes from HTML list & apply fallbacks ---
    const groupCodes = new Map<number, string[]>();
    const seenHtmlIatas = new Set<string>();

    for (const [iata] of airportsList) {
        const airportInfo = airportDb.get(iata.toUpperCase());
        if (!airportInfo || !airportInfo.correctedTz) continue;
        try {
            // *** Use memoized version ***
            const dstDetails = memoizedFindDstTransitions(airportInfo.correctedTz, year);
            if (dstDetails) {
                const stdOffset = dstDetails[0];
                const codes = groupCodes.get(stdOffset) || [];
                if (!seenHtmlIatas.has(airportInfo.iata)) {
                    codes.push(airportInfo.iata);
                    seenHtmlIatas.add(airportInfo.iata);
                    groupCodes.set(stdOffset, codes);
                }
            }
        } catch { /* Ignore airports whose TZ causes errors here */ }
    }

    // Trim HTML groups to groupSize
    if (groupSize > 0) {
        for (const stdOffset of groupCodes.keys()) {
            groupCodes.set(stdOffset, groupCodes.get(stdOffset)!.slice(0, groupSize));
        }
    }

    // Apply fallbacks for standard offsets that exist in buckets but have no HTML codes
    for (const stdOffset of groupKeys.keys()) {
        if (!groupCodes.has(stdOffset) || groupCodes.get(stdOffset)!.length === 0) {
            const fallbacks = getFallbackCodes(stdOffset);
            if (fallbacks.length > 0) {
                groupCodes.set(stdOffset, fallbacks);
                console.log(`Applied fallback for std offset ${stdOffset/3600}h: ${fallbacks.join(', ')}`);
            } else {
                console.warn(`No fallback codes found for std offset ${stdOffset/3600}h`);
            }
        }
    }
    console.log(`Grouped codes from HTML/fallbacks into ${groupCodes.size} standard offset groups.`);

    // --- Assign codes to final buckets ---
    const usedCodes = new Set<string>();

    const assignToBucket = (iataCode: string): boolean => {
        if (usedCodes.has(iataCode)) return false;
        const airportInfo = airportDb.get(iataCode);
        if (!airportInfo || !airportInfo.correctedTz) return false;

        try {
            // *** Use memoized version ***
            const dstDetails = memoizedFindDstTransitions(airportInfo.correctedTz, year);
            if (!dstDetails) return false;
            const key = getBucketKey(dstDetails);
            const bucket = tzBuckets.get(key);

            if (bucket) {
                // Apply maxBucket limit here during assignment
                if (maxBucket <= 0 || bucket.codes.length < maxBucket) {
                    bucket.codes.push(iataCode);
                    usedCodes.add(iataCode);
                    return true;
                }
            }
        } catch {
            // Ignore errors during assignment
        }
        return false;
    };

    // Assign codes from the HTML/fallback groups
    for (const codes of groupCodes.values()) {
        for (const iata of codes) {
            assignToBucket(iata);
        }
    }

    // --- Final safety pass for empty buckets ---
    console.log('Running safety pass for empty buckets...');
    let safetyPassAssigned = 0;
    for (const bucket of tzBuckets.values()) {
        if (bucket.codes.length === 0) {
            // Find potential candidates actually in this bucket's zones
            const candidates = Array.from(airportDb.values())
                .filter(a => bucket.tzNames.has(a.correctedTz) && !usedCodes.has(a.iata))
                .sort((a, b) => b.route_hits - a.route_hits);

            if (candidates.length > 0) {
                // Try assigning the best candidate
                if (assignToBucket(candidates[0].iata)) {
                    safetyPassAssigned++;
                }
            }
        }
    }
    console.log(`Safety pass assigned codes to ${safetyPassAssigned} previously empty buckets.`);

    // Apply maxBucket limit definitively after all assignments
    if (maxBucket > 0) {
        for (const bucket of tzBuckets.values()) {
            if (bucket.codes.length > maxBucket) {
                bucket.codes = bucket.codes.slice(0, maxBucket);
            }
        }
    }

    // --- Finalize Buckets and Generate Pools ---
    const sortedBuckets = Array.from(tzBuckets.values()).sort((a, b) => {
        if (a.std !== b.std) return a.std - b.std;
        if (a.dst !== b.dst) return a.dst - b.dst;
        return a.start - b.start;
    });

    const codePool: string[] = [];
    const namePool: string[] = [];
    const poolCodeSet = new Set<string>(); // Track codes added to pool

    // Calculate final offsets and counts
    for (const bucket of sortedBuckets) {
        const uniqueCodesForBucket: string[] = [];
        for (const code of bucket.codes) {
            if (!poolCodeSet.has(code)) {
                uniqueCodesForBucket.push(code);
                poolCodeSet.add(code);
            }
        }

        bucket.offset = codePool.length; // Offset is current size before adding
        bucket.count = uniqueCodesForBucket.length;

        // Add unique codes and their names to the pools
        for (const code of uniqueCodesForBucket) {
            codePool.push(code);
            const airportInfo = airportDb.get(code);
            let name = airportInfo?.name || code; // Fallback to code if name missing
            // Clean name
            if (name.endsWith(' International Airport')) {
                name = name.substring(0, name.length - ' International Airport'.length);
            } else if (name.endsWith(' Airport')) {
                name = name.substring(0, name.length - ' Airport'.length);
            }
            namePool.push(name.trim().replace(/\"/g, '\\"')); // Escape quotes for C string
        }
    }

    // --- Generate C Code String ---
    let cContent = `// Auto-generated by generateAirportTzList.ts\n`;
    cContent += `// Generated on: ${new Date().toISOString()}\n`;
    cContent += `// Year-specific DST data for ${year}\n\n`;
    cContent += `#include <stdint.h>\n\n`;

    // Airport Code Pool (3-letter IATA codes)
    cContent += `// Total airport codes: ${codePool.length}\n`;
    cContent += `static const char airport_code_pool[] =\n`;
    if (codePool.length > 0) {
        for (let i = 0; i < codePool.length; i++) {
            if (i % 8 === 0) cContent += `    `; // Indent
            cContent += `"${codePool[i]}"`;
            if ((i + 1) % 8 === 0 || i === codePool.length - 1) {
                cContent += `\n`;
            } else {
                cContent += ` `;
            }
        }
        cContent += `;\n\n`;
    } else {
        cContent += `    ""; // Empty pool\n`;
        cContent += `;

`;
    }

    // Airport Name Pool (pointers to strings)
    cContent += `// Total airport names: ${namePool.length}\n`;
    cContent += `static const char* airport_name_pool[] = {\n`;
    if (namePool.length > 0) {
        for (const name of namePool) {
            cContent += `    "${name}",\n`;
        }
    } else {
         cContent += `    // Empty pool\n`;
    }
    cContent += `};\n\n`;

    // TzInfo struct definition
    cContent += `typedef struct {\n`;
    cContent += `    float std_offset_hours;\n`;
    cContent += `    float dst_offset_hours;\n`;
    cContent += `    int64_t dst_start_utc;\n`;
    cContent += `    int64_t dst_end_utc;\n`;
    cContent += `    int name_offset; // Index into airport_code_pool & airport_name_pool\n`;
    cContent += `    int name_count;  // Number of unique airports for this tz variant\n`;
    cContent += `} TzInfo;\n\n`;

    // airport_tz_list array initialization
    cContent += `// Total timezone variants: ${sortedBuckets.length}\n`;
    cContent += `static const TzInfo airport_tz_list[] = {\n`;
    if (sortedBuckets.length > 0) {
        for (const bucket of sortedBuckets) {
            const std_h = bucket.std / 3600.0;
            const dst_h = bucket.dst / 3600.0;
            // Escape any backslashes potentially in timezone names for the comment
            const tzComment = Array.from(bucket.tzNames).slice(0,3).join(', ').replace(/\\/g, '\\');
            cContent += `    { ${std_h.toFixed(2)}f, ${dst_h.toFixed(2)}f, ${bucket.start}LL, ${bucket.end}LL, ${bucket.offset ?? 0}, ${bucket.count ?? 0} }, // ${tzComment}...\n`;
        }
    } else {
         cContent += `    // Empty list\n`;
    }
    cContent += `};\n\n`;

    // Definitions for counts
    cContent += `#define AIRPORT_TZ_LIST_COUNT (sizeof(airport_tz_list)/sizeof(airport_tz_list[0]))\n`;
    cContent += `#define AIRPORT_CODE_POOL_COUNT ${codePool.length}\n`;
    cContent += `#define AIRPORT_NAME_POOL_COUNT ${namePool.length}\n`;

    // --- Write C Code to File ---
    // --- 7. Write C Code to File --- 
    await fs.writeFile(outPath, cContent, 'utf-8');

    console.log(`Successfully generated ${outPath} with ${sortedBuckets.length} tz buckets and ${codePool.length} unique airports.`);
}

async function main() {
    program
        .version('1.0.0')
        .description('Generate airport_tz_list.c: hybrid grouping, DST buckets, fallback.')
        .option('--html <path>', 'Path to GetToCenter HTML file (top1000.html)', path.join(__dirname, 'top1000.html'))
        .option('--out <path>', 'C output file path', path.join(__dirname, '../src/c/airport_tz_list.c'))
        .option('--top <number>', 'Number of airports per std offset group (from HTML)', (val) => parseInt(val, 10), 10)
        .option('--max-bucket <number>', 'Max unique airports per DST bucket', (val) => parseInt(val, 10), 10)
        .parse(process.argv);

    const options = program.opts();

    console.log('Starting airport timezone list generation...');
    console.log('Options:', options);

    // Validate HTML file existence
    try {
        await fs.access(options.html);
    } catch (error) {
        console.error(`ERROR: HTML file not found: ${options.html}`);
        process.exit(1);
    }

    // Parse HTML
    const airportsList = await parseTopHtml(options.html);
    if (!airportsList || airportsList.length === 0) {
        console.error(`ERROR: No airports found in HTML: ${options.html}`);
        process.exit(1);
    }

    // Generate C code
    try {
        await generateCCode(airportsList, options.out, options.top, options.maxBucket);
        console.log('Airport timezone list generation finished successfully.');
    } catch (error) {
        console.error('Airport timezone list generation failed:', error);
        process.exit(1);
    }
}

// ------------------------------------------------------------
// Exports for unit testing
// ------------------------------------------------------------
export {
    generateCCode,
    memoizedFindDstTransitions,
    memoizedFindTz,
};

// ------------------------------------------------------------
// CLI entry point – only run when invoked directly
// ------------------------------------------------------------
if (require.main === module) {
    main().catch(error => {
        console.error('An unhandled error occurred:', error);
        process.exit(1);
    });
}
