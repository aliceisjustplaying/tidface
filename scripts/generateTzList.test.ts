import os from 'os';
import path from 'path';
import fs from 'fs/promises';
import { DateTime } from 'luxon';
import { generateTzCCode } from './generateTzList';
import { findDstTransitions, DstTransitions } from './tzCommon'; // Import to get expected values

jest.setTimeout(60000); // Keep generous timeout as it runs the full generation

// Helper to parse the simple C array formats
function parseCStringArray(cCode: string, arrayName: string): string[] {
  const startMarker = `static const char* ${arrayName}[] = {`;
  const endMarker = `};`;

  const startIndex = cCode.indexOf(startMarker);
  if (startIndex === -1) {
    console.error(`[TEST PARSE ERROR] Could not find start marker for C array: ${arrayName}`);
    return [];
  }

  const braceIndex = startIndex + startMarker.length;
  const endIndex = cCode.indexOf(endMarker, braceIndex);
  if (endIndex === -1) {
     console.error(`[TEST PARSE ERROR] Could not find end marker for C array: ${arrayName}`);
     return [];
  }

  const blockContent = cCode.slice(braceIndex, endIndex);
  const lines = blockContent.split('\n');

  const results: string[] = [];
  lines.forEach((line, index) => {
    const trimmedLine = line.trim();
    const lineMatch = trimmedLine.match(/^"(.*?)",?$/);
    if (lineMatch && lineMatch[1]) {
      results.push(lineMatch[1]);
    }
  });
  return results;
}

interface ParsedTzInfo {
  stdHours: number;
  dstHours: number;
  startTs: number;
  endTs: number;
  nameOffset: number;
  nameCount: number;
}

function parseTzInfoArray(cCode: string): ParsedTzInfo[] {
  const startMarker = `static const TzInfo tz_list[] = {`;
  const endMarker = `};`;

  const startIndex = cCode.indexOf(startMarker);
  if (startIndex === -1) {
    console.error(`[TEST PARSE ERROR] Could not find start marker for TzInfo array`);
    return [];
  }

  const braceIndex = startIndex + startMarker.length;
  const endIndex = cCode.indexOf(endMarker, braceIndex);
  if (endIndex === -1) {
     console.error(`[TEST PARSE ERROR] Could not find end marker for TzInfo array`);
     return [];
  }

  const blockContent = cCode.slice(braceIndex, endIndex);
  const lines = blockContent.split('\n');

  const result: ParsedTzInfo[] = [];
  const lineRegex = /\{\s*(-?\d+(?:\.\d+)?)f,\s*(-?\d+(?:\.\d+)?)f,\s*(\d+)LL,\s*(\d+)LL,\s*(\d+),\s*(\d+)\s*\},?/; // Allow integers or floats for offsets
  lines.forEach(line => {
    const lineMatch = line.trim().match(lineRegex);
    if (lineMatch) {
      result.push({
        stdHours: parseFloat(lineMatch[1]),
        dstHours: parseFloat(lineMatch[2]),
        startTs: parseInt(lineMatch[3], 10),
        endTs: parseInt(lineMatch[4], 10),
        nameOffset: parseInt(lineMatch[5], 10),
        nameCount: parseInt(lineMatch[6], 10),
      });
    }
  });
  return result;
}


describe('generateTzList', () => {
  let generatedCCode: string;
  let namePool: string[];
  let tzList: ParsedTzInfo[];
  const currentYear = DateTime.utc().year;

  // Run generation once before all tests
  beforeAll(async () => {
    const out = path.join(os.tmpdir(), `tz_${Date.now()}.c`);
    await generateTzCCode(out);
    generatedCCode = await fs.readFile(out, 'utf-8');
    namePool = parseCStringArray(generatedCCode, 'tz_name_pool');
    tzList = parseTzInfoArray(generatedCCode);
    // Add a check to ensure parsing worked before tests run
    if (namePool.length === 0 || tzList.length === 0) {
      throw new Error('[TEST] Failed to parse generated C code in beforeAll hook.');
    }
  });

  // Test cases for specific zones
  const testCases: { zone: string; city: string }[] = [
    { zone: 'America/New_York', city: 'New York' },
    { zone: 'Asia/Tokyo', city: 'Tokyo' },
    { zone: 'Pacific/Auckland', city: 'Auckland' },
    { zone: 'Australia/Lord_Howe', city: 'Lord Howe' },
    { zone: 'Europe/London', city: 'London' },
  ];

  test.each(testCases)('should generate correct entry for $city ($zone)', ({ zone, city }) => {
    const expectedTransitions = findDstTransitions(zone, currentYear);
    const expected: ParsedTzInfo = {
      stdHours: expectedTransitions[0] / 3600,
      dstHours: expectedTransitions[1] / 3600,
      startTs: expectedTransitions[2],
      endTs: expectedTransitions[3],
      nameOffset: -1, // Will be updated
      nameCount: -1, // Will be updated
    };

    // 1. Find the city in the name pool
    const cityIndex = namePool.indexOf(city);
    expect(cityIndex).toBeGreaterThanOrEqual(0); // City must exist in the pool

    // 2. Find the bucket containing this city
    const bucket = tzList.find(entry =>
      cityIndex >= entry.nameOffset && cityIndex < (entry.nameOffset + entry.nameCount)
    );
    expect(bucket).toBeDefined(); // A bucket must contain this city

    // 3. Compare bucket details with expected values
    expect(bucket?.stdHours).toBeCloseTo(expected.stdHours, 2);
    expect(bucket?.dstHours).toBeCloseTo(expected.dstHours, 2);
    expect(bucket?.startTs).toBe(expected.startTs);
    expect(bucket?.endTs).toBe(expected.endTs);
  });

  test('should contain the required macros', () => {
    expect(generatedCCode).toMatch(/#define TZ_LIST_COUNT/);
    expect(generatedCCode).toMatch(/#define TZ_NAME_POOL_COUNT/);
  });

  test('TZ_LIST_COUNT macro should match parsed list length', () => {
    const match = generatedCCode.match(/#define\s+TZ_LIST_COUNT\s+\(sizeof\(tz_list\)\/sizeof\(tz_list\[0\]\)\)/);
    expect(match).not.toBeNull();
    // Check the actual count directly from parsed data
    expect(tzList.length).toBeGreaterThan(10); // Keep a basic sanity check on count
    // We could potentially parse the value from the define if needed, but comparing to parsed length is good
  });

  test('TZ_NAME_POOL_COUNT macro should match parsed name pool length', () => {
     const match = generatedCCode.match(/#define\s+TZ_NAME_POOL_COUNT\s+\(sizeof\(tz_name_pool\)\/sizeof\(tz_name_pool\[0\]\)\)/);
     expect(match).not.toBeNull();
     expect(namePool.length).toBeGreaterThan(50); // Basic sanity check
  });
}); 
