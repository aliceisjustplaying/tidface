import * as fs from 'fs/promises';
import * as cheerio from 'cheerio';
import { find as findTz } from 'geo-tz';
import { parse as parseSync } from 'csv-parse/sync';
import { findDstTransitions, DstTransitions } from './tzCommon';

// ---------------------------------------------------------------------------
// Memoization Caches
// ---------------------------------------------------------------------------
export const findTzCache = new Map<string, string[]>();
export const findDstTransitionsCache = new Map<string, DstTransitions | null>();

// ---------------------------------------------------------------------------
// Helper Functions
// ---------------------------------------------------------------------------
/** Memoize geo-tz lookups */
export function memoizedFindTz(latitude: number, longitude: number): string[] {
    const key = `${latitude}_${longitude}`;
    if (findTzCache.has(key)) return findTzCache.get(key)!;
    try {
        const result = findTz(latitude, longitude);
        findTzCache.set(key, result);
        return result;
    } catch {
        findTzCache.set(key, []);
        return [];
    }
}

/** Memoize DST transition lookups */
export function memoizedFindDstTransitions(tz: string, year: number): DstTransitions | null {
    if (!tz) return null;
    const key = `${tz}_${year}`;
    if (findDstTransitionsCache.has(key)) return findDstTransitionsCache.get(key)!;
    try {
        const result = findDstTransitions(tz, year);
        findDstTransitionsCache.set(key, result);
        return result;
    } catch {
        findDstTransitionsCache.set(key, null);
        return null;
    }
}

// ---------------------------------------------------------------------------
// Type Definitions
// ---------------------------------------------------------------------------
/** Relevant classification data from OurAirports */
export interface OurAirportInfo {
    iata: string;
    type: string;
    scheduled_service: string;
}

/** Internal airport record with metadata */
export interface AirportInfo {
    iata: string;
    name: string;
    city: string;
    country: string;
    latitude: number;
    longitude: number;
    tz: string;
    correctedTz: string;
    type?: string;
    source?: string;
    scheduled_service?: string;
    route_hits: number;
}

/** Single route record [src, dst] */
export type RouteRecord = [string, string];

/** Data for one timezone bucket */
export interface TzBucketData {
    std: number;
    dst: number;
    start: number;
    end: number;
    tzNames: Set<string>;
    codes: string[];
    offset?: number;
    count?: number;
}

// ---------------------------------------------------------------------------
// Helper: Bucket Key
// ---------------------------------------------------------------------------
/** Construct a unique key from DST transition details */
export function getBucketKey(details: DstTransitions): string {
    return `${details[0]}_${details[1]}_${details[2]}_${details[3]}`;
}

// ---------------------------------------------------------------------------
// HTML Parsing
// ---------------------------------------------------------------------------
/** Parse the top1000 HTML table for (IATA, Name) pairs */
export async function parseTopHtml(htmlPath: string): Promise<Array<[string, string]>> {
    console.log(`Parsing HTML file: ${htmlPath}`);
    const htmlContent = await fs.readFile(htmlPath, 'utf-8');
    const $ = cheerio.load(htmlContent);
    const results: Array<[string, string]> = [];
    $('tr').each((_, tr) => {
        const tds = $(tr).find('td');
        if (tds.length < 3) return;
        const iata = $(tds[2]).text().trim().toUpperCase();
        if (!iata || iata.length !== 3) return;
        let name = $(tds[1]).find('h2').first().text().trim();
        if (!name) {
            name = $(tds[1]).text().trim();
        }
        if (name.endsWith(' International Airport')) {
            name = name.slice(0, -' International Airport'.length);
        } else if (name.endsWith(' Airport')) {
            name = name.slice(0, -' Airport'.length);
        }
        results.push([iata, name.trim()]);
    });
    console.log(`Found ${results.length} airports in HTML.`);
    return results;
}

// ---------------------------------------------------------------------------
// Routes Data
// ---------------------------------------------------------------------------
/** Download and parse OpenFlights routes.dat */
export async function downloadRoutesCsv(): Promise<RouteRecord[]> {
    console.log('Downloading and parsing routes data...');
    const url = 'https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat';
    try {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`Failed to fetch ${url}: ${response.statusText}`);
        const text = await response.text();
        console.log('Routes data downloaded, parsing...');
        // Parse CSV synchronously to avoid lingering parser handles
        const rawRecords = parseSync(text, { delimiter: ',', columns: false, skip_empty_lines: true, trim: true }) as string[][];
        // Build RouteRecord[] explicitly to satisfy tuple typing
        const records = rawRecords.reduce<RouteRecord[]>((acc, record) => {
            const srcIata = record[2];
            const dstIata = record[4];
            if (
                srcIata.length === 3 && /^[A-Z]+$/.test(srcIata) &&
                dstIata.length === 3 && /^[A-Z]+$/.test(dstIata)
            ) {
                acc.push([srcIata, dstIata]);
            }
            return acc;
        }, []);
        console.log(`Finished parsing routes. Found ${records.length} valid routes.`);
        return records;
    } catch (error) {
        console.error('Error downloading or parsing routes data:', error);
        throw error;
    }
}

/** Rank airports by total route hits */
export async function rankAirportsByRoutes(routes: RouteRecord[]): Promise<Map<string, number>> {
    console.log('Ranking airports by routes...');
    const counts = new Map<string, number>();
    for (const [src, dst] of routes) {
        counts.set(src, (counts.get(src) || 0) + 1);
        counts.set(dst, (counts.get(dst) || 0) + 1);
    }
    console.log(`Airport ranking complete. Found ${counts.size} unique airports in routes.`);
    return counts;
}

// ---------------------------------------------------------------------------
// OurAirports Data
// ---------------------------------------------------------------------------
/** Download and parse OurAirports CSV */
export async function downloadOurAirportsCsv(): Promise<Map<string, OurAirportInfo>> {
    console.log('Downloading and parsing OurAirports data...');
    const url = 'https://davidmegginson.github.io/ourairports-data/airports.csv';
    const ourAirportsMap = new Map<string, OurAirportInfo>();
    try {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`Failed to fetch ${url}: ${response.statusText}`);
        const text = await response.text();
        console.log('OurAirports data downloaded, parsing...');
        // Parse CSV synchronously to avoid lingering parser handles
        const rawRecords: Array<Record<string, string>> = parseSync(text, { delimiter: ',', columns: true, skip_empty_lines: true, trim: true });
        rawRecords.forEach((record: any) => {
            const iata = record.iata_code;
            const type = record.type;
            const scheduled = record.scheduled_service;
            if (iata?.length === 3 && /^[A-Z0-9]+$/.test(iata)) {
                ourAirportsMap.set(iata.toUpperCase(), { iata: iata.toUpperCase(), type: type || '', scheduled_service: scheduled || '' });
            }
        });
        console.log(`Finished parsing OurAirports. Found ${ourAirportsMap.size} airports with IATA.`);
        return ourAirportsMap;
    } catch (error) {
        console.error('Error downloading or parsing OurAirports data:', error);
        console.warn('Proceeding without OurAirports classification data.');
        return ourAirportsMap;
    }
} 
