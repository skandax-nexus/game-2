import test from 'node:test';
import assert from 'node:assert/strict';
import { sunriseForDate, nextSunrise } from '../src/sunrise.js';

const minutesOff = (actual, expectedIso) => Math.abs(actual.getTime() - new Date(expectedIso).getTime()) / 60000;

test('London midsummer sunrise is about 03:43 UTC', () => {
  assert.ok(minutesOff(sunriseForDate(2026, 5, 21, 51.5074, -0.1278), '2026-06-21T03:43:00Z') < 5);
});

test('New York new year sunrise is about 12:20 UTC', () => {
  assert.ok(minutesOff(sunriseForDate(2026, 0, 1, 40.7128, -74.006), '2026-01-01T12:20:00Z') < 5);
});

test('Chennai sunrise in October is about 00:30 UTC', () => {
  assert.ok(minutesOff(sunriseForDate(2026, 9, 20, 13.0827, 80.2707), '2026-10-20T00:28:00Z') < 8);
});

test('polar night has no sunrise and lives fall back to 24 hours', () => {
  assert.equal(sunriseForDate(2026, 11, 21, 78.22, 15.65), null);
  const now = new Date('2026-12-21T10:00:00Z');
  assert.equal(nextSunrise(now, 78.22, 15.65).getTime() - now.getTime(), 24 * 3600000);
});

test('a life always lasts at least the minimum and at most about a day and a half', () => {
  for (let h = 0; h < 24; h++) {
    const now = new Date(Date.UTC(2026, 9, 20, h, 17));
    for (const [lat, lng] of [[51.5, -0.12], [13.08, 80.27], [-33.87, 151.2], [40.71, -74]]) {
      const hours = (nextSunrise(now, lat, lng, 12).getTime() - now.getTime()) / 3600000;
      assert.ok(hours >= 12 && hours <= 36, `${hours}h at ${lat},${lng} from ${now.toISOString()}`);
    }
  }
});
