import path from 'path';
import os from 'os';
import fs from 'fs/promises';
import { generateCCode } from './generateAirportTzList';

// ---------------------------------------------------------------------------
// Jest setup: mock external dependencies (airport-data package and fetch)
// ---------------------------------------------------------------------------

jest.mock('airport-data', () => {
  // Minimal stub covering the three airports we use in tests
  return [
    {
      iata: 'JFK',
      name: 'John F. Kennedy International Airport',
      city: 'New York',
      country: 'US',
      latitude: 40.6413,
      longitude: -73.7781,
      tz: 'America/New_York',
      type: 'large_airport',
    },
    {
      iata: 'LHR',
      name: 'London Heathrow Airport',
      city: 'London',
      country: 'GB',
      latitude: 51.4700,
      longitude: -0.4543,
      tz: 'Europe/London',
      type: 'large_airport',
    },
    {
      iata: 'FEN',
      name: 'Fernando de Noronha Airport',
      city: 'Fernando de Noronha',
      country: 'BR',
      latitude: -3.8549,
      longitude: -32.4233,
      tz: 'America/Fortaleza', // Intentionally wrong to test correction logic
      type: 'large_airport',
    },
  ];
});

// Mock global fetch so that CSV downloads are fast & deterministic
const fakeRoutesCSV = `0,0,JFK,0,LHR,0,0,0,0,0\n1,0,LHR,0,JFK,0,0,0,0,0\n`;
const fakeOurAirportsCSV = `id,ident,type,name,latitude_deg,longitude_deg,iso_country,iata_code,scheduled_service\n1,ABC,large_airport,John F Kennedy,0,0,US,JFK,yes\n2,DEF,large_airport,London Heathrow,0,0,GB,LHR,yes\n3,GHI,large_airport,Fernando de Noronha,0,0,BR,FEN,yes\n`;

global.fetch = jest.fn(async (url: string) => {
  if (url.includes('routes.dat')) {
    return {
      ok: true,
      text: async () => fakeRoutesCSV,
    } as any;
  }
  if (url.includes('airports.csv')) {
    return {
      ok: true,
      text: async () => fakeOurAirportsCSV,
    } as any;
  }
  throw new Error(`Unexpected fetch to ${url}`);
}) as any;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

afterAll(() => {
  // @ts-expect-error restore fetch
  delete global.fetch;
});

const tmpFile = () => path.join(os.tmpdir(), `airport_tz_${Date.now()}.c`);

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('generateAirportTzList', () => {
  jest.setTimeout(15000); // allow a bit more time for Luxon calculations

  const airportsList: Array<[string, string]> = [
    ['JFK', 'John F. Kennedy International Airport'],
    ['LHR', 'London Heathrow Airport'],
    ['FEN', 'Fernando de Noronha Airport'],
  ];

  test('generateCCode writes a C file containing all unique airport codes', async () => {
    const out = tmpFile();
    await generateCCode(airportsList, out, 5, 5);

    const content = await fs.readFile(out, 'utf-8');

    // Expect the three airport codes to be present exactly once each in the code pool section
    ['JFK', 'LHR', 'FEN'].forEach(code => {
      // Codes are encoded as bit-packed entries with comments like /* JFK */
      const matches = content.match(new RegExp(`\\/\\*\\s*${code}\\s*\\*\\/`, 'g'));
      expect(matches).not.toBeNull();
      expect(matches!.length).toBe(1);
    });

    // Ensure the Noronha timezone correction took place – should show up in bucket comment
    expect(content).toContain('America/Noronha');

    // Validate the macro count equals 3 (unique codes)
    const poolCountMatch = content.match(/#define\s+AIRPORT_CODE_POOL_COUNT\s+(\d+)/);
    expect(poolCountMatch).not.toBeNull();
    expect(Number(poolCountMatch![1])).toBe(3);
  });

  test('generateCCode creates distinct buckets for each standard offset', async () => {
    const out = tmpFile();
    await generateCCode(airportsList, out, 5, 5);
    const content = await fs.readFile(out, 'utf-8');

    // Extract quarter-hour std offsets (first field in each TzInfo entry)
    const stdOffsetQuarters = Array.from(content.matchAll(/\{\s*([+-]?\d+),/g)).map(m => Number(m[1]));
    // Convert to hours by dividing by 4
    const stdOffsets = stdOffsetQuarters.map(q => q / 4);
    // Expect three distinct standard offsets: -5, 0, -2 hours
    expect(new Set(stdOffsets)).toEqual(new Set([-5, 0, -2]));
  });
}); 
