import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));

// The story lives inside the Unreal project so the game client and server share one source of truth.
export const DEFAULT_STORY_PATH = path.resolve(here, '../../Game/Content/Data/Story/Season1.json');

export function loadStory(file = DEFAULT_STORY_PATH) {
  return JSON.parse(readFileSync(file, 'utf8'));
}

export function seasonDay(seasonStart, now) {
  return Math.max(1, Math.floor((now.getTime() - seasonStart.getTime()) / 86400000) + 1);
}

export function actForDay(story, day) {
  const act = story.acts.find((a) => day >= a.startDay && day <= a.endDay);
  if (act) return act;
  return {
    id: story.acts.length + 1,
    name: 'The Reading of the Will',
    startDay: story.lengthDays + 1,
    endDay: story.lengthDays + 1,
    summary: 'The season is over. The Will has been read.',
    ledgerPageChance: 0,
    unlocksFactions: true,
    finale: true,
  };
}

export function unlockedTwists(story, day) {
  return story.twists.filter((t) => day >= t.day);
}

export function factionsUnlocked(story, day) {
  return story.acts.some((a) => a.unlocksFactions && day >= a.startDay);
}
