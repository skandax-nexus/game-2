// Sunrise times from the NOAA "Almanac for Computers" algorithm.
// Accurate to a couple of minutes, which is plenty for deciding when a life ends.

const RAD = Math.PI / 180;
const DEG = 180 / Math.PI;
const ZENITH = 90.833; // official sunrise, including refraction and the sun's radius

const norm = (v, max) => ((v % max) + max) % max;

function dayOfYear(y, m, d) {
  return Math.round((Date.UTC(y, m, d) - Date.UTC(y, 0, 0)) / 86400000);
}

/**
 * Sunrise in UTC for the given calendar date (UTC fields) at a location.
 * Returns null when the sun does not rise that day (polar night or midnight sun).
 */
export function sunriseForDate(year, month, day, lat, lng) {
  const n = dayOfYear(year, month, day);
  const lngHour = lng / 15;
  const t = n + (6 - lngHour) / 24;
  const meanAnomaly = 0.9856 * t - 3.289;
  const trueLng = norm(
    meanAnomaly + 1.916 * Math.sin(meanAnomaly * RAD) + 0.02 * Math.sin(2 * meanAnomaly * RAD) + 282.634,
    360,
  );
  let ra = norm(DEG * Math.atan(0.91764 * Math.tan(trueLng * RAD)), 360);
  ra += Math.floor(trueLng / 90) * 90 - Math.floor(ra / 90) * 90;
  ra /= 15;

  const sinDec = 0.39782 * Math.sin(trueLng * RAD);
  const cosDec = Math.cos(Math.asin(sinDec));
  const cosH = (Math.cos(ZENITH * RAD) - sinDec * Math.sin(lat * RAD)) / (cosDec * Math.cos(lat * RAD));
  if (cosH > 1 || cosH < -1) return null;

  const h = (360 - DEG * Math.acos(cosH)) / 15;
  const localMeanTime = h + ra - 0.06571 * t - 6.622;
  const ut = norm(localMeanTime - lngHour, 24);
  return new Date(Date.UTC(year, month, day) + ut * 3600000);
}

/**
 * The moment a life that starts at `now` ends: the first local sunrise at least
 * `minLifeHours` away. Where the sun won't rise, a life lasts exactly 24 hours.
 */
export function nextSunrise(now, lat, lng, minLifeHours = 12) {
  const threshold = now.getTime() + minLifeHours * 3600000;
  const candidates = [];
  for (let offset = -1; offset <= 3; offset++) {
    const d = new Date(now.getTime() + offset * 86400000);
    const s = sunriseForDate(d.getUTCFullYear(), d.getUTCMonth(), d.getUTCDate(), lat, lng);
    if (s) candidates.push(s.getTime());
  }
  candidates.sort((a, b) => a - b);
  const found = candidates.find((c) => c >= threshold);
  return new Date(found ?? now.getTime() + 24 * 3600000);
}
