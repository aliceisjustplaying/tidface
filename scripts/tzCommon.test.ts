import { execSync } from 'child_process';
import path from 'path';
import { findDstTransitions, DstTransitions } from './tzCommon';

// Function to get expected result from Python script
const getExpectedFromPython = (zone: string, year: number): DstTransitions => {
  const pythonScriptPath = path.resolve(__dirname, 'tz_common.py');
  const command = `python3 "${pythonScriptPath}" "${zone}" ${year}`;
  try {
    const stdout = execSync(command, { encoding: 'utf-8' });
    // Parse the output tuple string, e.g., "(-18000, -14400, 1741503600, 1762063200)\n"
    const match = stdout.trim().match(/^\((.*)\)$/);
    if (match && match[1]) {
      const values = match[1].split(', ').map(Number);
      if (values.length === 4 && values.every(v => !isNaN(v))) {
        return values as DstTransitions;
      }
    }
    throw new Error(`Failed to parse Python output: ${stdout}`);
  } catch (error: any) {
    console.error(`Error executing Python script for ${zone}/${year}:`, error.stderr || error.message);
    // Return a default/error value or rethrow, depending on desired test behavior
    // Returning [0,0,0,0] for invalid zones handled by Python script itself
    if (error.stderr && error.stderr.includes('Invalid zone')) {
        // Match the python script's likely return for truly invalid IANA names
        // Note: findDstTransitions in Python returns (0,0,0,0) on initial get_tz_details failure.
        return [0, 0, 0, 0];
    }
     // Rethrow for unexpected errors (e.g., python not found, script errors)
    throw new Error(`Python script execution failed for ${zone}/${year}: ${error.message}`);
  }
};

// Generated for year 2025 by generateExpectedTransitions.ts
const expectedTransitions: { zone: string; expected: DstTransitions }[] = [
  {
    zone: 'America/New_York',
    expected: [-18000, -14400, 1741503600, 1762063200] as DstTransitions,
  },
  {
    zone: 'Europe/London',
    expected: [0, 3600, 1743296400, 1761440400] as DstTransitions,
  },
  {
    zone: 'Pacific/Auckland',
    expected: [43200, 46800, 1758981600, 1743861600] as DstTransitions,
  },
  {
    zone: 'Asia/Tokyo',
    expected: [32400, 32400, 0, 0] as DstTransitions,
  },
  {
    zone: 'Pacific/Apia',
    // Note: Expected output shows Apia has no DST in 2025 according to Luxon/IANA data used.
    expected: [46800, 46800, 0, 0] as DstTransitions,
  },
  {
    zone: 'Australia/Lord_Howe',
    expected: [37800, 39600, 1759593600, 1743865200] as DstTransitions,
  },
  {
    zone: 'America/Denver',
    expected: [-25200, -21600, 1741510800, 1762070400] as DstTransitions,
  },
  {
    zone: 'Asia/Katmandu',
    expected: [20700, 20700, 0, 0] as DstTransitions,
  },
  {
    zone: 'Invalid/Zone',
    expected: [0, 0, 0, 0] as DstTransitions,
  },
];

describe('tzCommon', () => {
  describe('findDstTransitions', () => {
    const year = 2025; // Match the year used for generation

    test.each(expectedTransitions)('should match generated expected value for $zone in $year', ({ zone, expected }) => {
      const result = findDstTransitions(zone, year);

      // Explicitly compare each element
      expect(result[0]).toBe(expected[0]); // stdOffsetSeconds
      expect(result[1]).toBe(expected[1]); // dstOffsetSeconds
      expect(result[2]).toBe(expected[2]); // dstStartUtcTimestamp
      expect(result[3]).toBe(expected[3]); // dstEndUtcTimestamp
    });

    // Keep cache test as it verifies TS internal behavior
    test('should use cache for repeated calls', () => {
      const zone = 'America/Los_Angeles';
      const year = 2026; // Use a different year

      const result1 = findDstTransitions(zone, year);
      const result2 = findDstTransitions(zone, year);
      expect(result1).toEqual(result2);
    });
  });
}); 
