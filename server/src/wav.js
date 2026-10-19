// Minimal validation for the 16-bit PCM WAV voice notes the game client uploads.

export function parseWav(buf) {
  if (buf.length < 44) throw new Error('file too small to be a WAV');
  if (buf.toString('ascii', 0, 4) !== 'RIFF' || buf.toString('ascii', 8, 12) !== 'WAVE') {
    throw new Error('not a RIFF/WAVE file');
  }
  let offset = 12;
  let fmt = null;
  let dataBytes = null;
  while (offset + 8 <= buf.length) {
    const id = buf.toString('ascii', offset, offset + 4);
    const size = buf.readUInt32LE(offset + 4);
    const body = offset + 8;
    if (id === 'fmt ') {
      fmt = {
        audioFormat: buf.readUInt16LE(body),
        channels: buf.readUInt16LE(body + 2),
        sampleRate: buf.readUInt32LE(body + 4),
        byteRate: buf.readUInt32LE(body + 8),
        bitsPerSample: buf.readUInt16LE(body + 14),
      };
    } else if (id === 'data') {
      dataBytes = Math.min(size, buf.length - body);
      break;
    }
    offset = body + size + (size % 2);
  }
  if (!fmt) throw new Error('missing fmt chunk');
  if (dataBytes === null) throw new Error('missing data chunk');
  if (fmt.audioFormat !== 1 || fmt.bitsPerSample !== 16) throw new Error('only 16-bit PCM is accepted');
  if (fmt.channels < 1 || fmt.channels > 2) throw new Error('only mono or stereo is accepted');
  if (fmt.sampleRate < 8000 || fmt.sampleRate > 48000) throw new Error('unsupported sample rate');
  const durationSeconds = dataBytes / (fmt.sampleRate * fmt.channels * 2);
  return { ...fmt, dataBytes, durationSeconds };
}
