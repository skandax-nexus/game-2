// Pure game rules: how estates are made, how actions change a life, and what an heir receives.
import { randomUUID } from 'node:crypto';

export class GameError extends Error {
  constructor(code, message, status = 400) {
    super(message);
    this.code = code;
    this.status = status;
  }
}

export const LEGACY_CHOICES = ['keep', 'sell', 'hide', 'destroy'];
export const ACTION_TYPES = [
  'pay_debt',
  'dodge_debt',
  'settle_feud',
  'start_feud',
  'chase_clue',
  'sell_item',
  'deliver_letter',
  'bribe_notary',
];

const BRIBE_COST = 50;
const SETTLE_COST = 30;
const LEDGER_SALE_PRICE = 300;
const SALE_RATE = 0.8;
const FAMILY_SECRET_HEIRS = 5;
const HISTORY_LENGTH = 12;

const newId = () => randomUUID().slice(0, 8);

function makeItem(template, extra = {}) {
  return {
    id: newId(),
    kind: template.kind,
    name: template.name,
    description: template.description,
    value: template.value ?? 0,
    hidden: false,
    data: {},
    ...extra,
  };
}

function makeClue(template, data = {}) {
  return { id: newId(), title: template.title, text: template.text, resolved: false, data };
}

function makeLedgerPage(story, number, rng) {
  const text = rng.pick(story.estateTables.ledgerPageTexts);
  return makeItem(
    { kind: 'ledger_page', name: `Ledger page ${number}`, description: text, value: 0 },
    { data: { number } },
  );
}

/** A brand-new estate for when no stranger's life is waiting to be inherited. */
export function generateStarterEstate(story, rng) {
  const t = story.estateTables;
  const estate = {
    money: rng.int(5, 30) * 10,
    house: { ...rng.pick(t.houses) },
    debts: [],
    items: [],
    feuds: [],
    clues: [],
    personalStory: null,
    history: [],
  };
  for (let i = rng.int(1, 2); i > 0; i--) {
    estate.debts.push({ id: newId(), creditor: rng.pick(t.creditors), amount: rng.int(4, 25) * 10, note: '' });
  }
  for (const item of rng.shuffle(t.items).slice(0, rng.int(2, 3))) estate.items.push(makeItem(item));
  if (rng.chance(0.5)) {
    estate.feuds.push({ id: newId(), rival: rng.pick(t.rivals), reason: rng.pick(t.feudReasons), severity: 'grudge' });
  }
  for (const clue of rng.shuffle(t.clues).slice(0, rng.int(1, 2))) estate.clues.push(makeClue(clue));

  if (rng.chance(0.6)) {
    const story_ = rng.pick(t.personalStories);
    estate.personalStory = { kind: story_.kind, title: story_.title, text: story_.text, heirs: 0, resolved: false };
    if (story_.item) estate.items.push(makeItem(story_.item, { data: { story: story_.kind, streak: 0 } }));
    if (story_.clue) estate.clues.push(makeClue(story_.clue, { story: story_.kind }));
    if (story_.feud) estate.feuds.push({ id: newId(), ...story_.feud, data: { story: story_.kind } });
  }
  return estate;
}

function find(list, id, what) {
  const found = list.find((x) => x.id === id);
  if (!found) throw new GameError('not_found', `No such ${what}.`, 404);
  return found;
}

function spend(state, amount) {
  if (state.money < amount) throw new GameError('not_enough_money', `You need ${amount} coins and have ${state.money}.`);
  state.money -= amount;
}

/**
 * Apply one of the ~10 actions a life gets. Mutates `state` (the life's working copy of the estate).
 * Returns a message for the player and any changes to the shared season.
 */
export function applyAction(state, action, ctx) {
  const { story, act, rng, season } = ctx;
  const delta = {};
  let message;

  switch (action?.type) {
    case 'pay_debt': {
      const debt = find(state.debts, action.debtId, 'debt');
      spend(state, debt.amount);
      state.debts = state.debts.filter((d) => d !== debt);
      message = `You paid ${debt.amount} coins to ${debt.creditor}. One less thing to pass on.`;
      break;
    }
    case 'dodge_debt': {
      const debt = find(state.debts, action.debtId, 'debt');
      debt.amount = Math.ceil(debt.amount * 1.5);
      debt.note = 'Dodged. Interest is climbing.';
      message = `You slipped past ${debt.creditor}'s collector. The debt is now ${debt.amount}.`;
      break;
    }
    case 'settle_feud': {
      const feud = find(state.feuds, action.feudId, 'feud');
      if (feud.severity === 'blood') {
        message = `You forgave ${feud.rival}. The revenge chain ends with you.`;
        if (state.personalStory?.kind === 'revenge_chain') state.personalStory.resolved = true;
      } else {
        spend(state, SETTLE_COST);
        message = `You bought ${feud.rival} a drink and ${SETTLE_COST} coins of goodwill. The feud is over.`;
      }
      state.feuds = state.feuds.filter((f) => f !== feud);
      break;
    }
    case 'start_feud': {
      const rival = String(action.rival ?? '').trim().slice(0, 60);
      if (!rival) throw new GameError('invalid', 'Name who you are starting a feud with.');
      state.feuds.push({ id: newId(), rival, reason: 'a feud you started', severity: 'grudge' });
      message = `You made an enemy of ${rival}. Your heir will remember it, even if you don't.`;
      break;
    }
    case 'chase_clue': {
      const clue = find(state.clues, action.clueId, 'clue');
      if (clue.resolved) throw new GameError('invalid', 'You already followed that clue.');
      clue.resolved = true;
      if (clue.data?.story === 'missing_child') {
        message = 'You found him at the north quay, staring at the ferry. He goes home before sunrise.';
        if (state.personalStory) state.personalStory.resolved = true;
      } else if (rng.chance(act.ledgerPageChance)) {
        const number = (season.pagesFound ?? 0) + 1;
        state.items.push(makeLedgerPage(story, number, rng));
        delta.pagesFound = 1;
        message = `"${clue.title}" led you to a torn page of the Ledger. Page ${number}. Guard it.`;
      } else {
        const coins = rng.chance(0.3) ? rng.int(2, 8) * 10 : 0;
        state.money += coins;
        message = coins
          ? `"${clue.title}" was a dead end, but you found ${coins} coins along the way.`
          : `"${clue.title}" was a dead end. Maybe your heir will see what you missed.`;
      }
      break;
    }
    case 'sell_item': {
      const item = find(state.items, action.itemId, 'item');
      if (item.kind === 'ledger_page') {
        state.money += LEDGER_SALE_PRICE;
        delta.pagesLost = 1;
        message = `House Thrane paid ${LEDGER_SALE_PRICE} coins for ${item.name}. It will never be seen again.`;
      } else {
        if (!item.value) throw new GameError('invalid', `Nobody will buy ${item.name}.`);
        const price = Math.floor(item.value * SALE_RATE);
        state.money += price;
        message = `You sold ${item.name} for ${price} coins.`;
      }
      state.items = state.items.filter((i) => i !== item);
      break;
    }
    case 'deliver_letter': {
      const item = find(state.items, action.itemId, 'item');
      if (item.kind !== 'letter') throw new GameError('invalid', 'That is not a letter.');
      state.items = state.items.filter((i) => i !== item);
      if (state.personalStory?.kind === 'unfinished_letter') state.personalStory.resolved = true;
      message = 'You finished the last line yourself and delivered the letter. Someone in Hollow Bay is crying.';
      break;
    }
    case 'bribe_notary': {
      spend(state, BRIBE_COST);
      const known = new Set(state.clues.map((c) => c.title));
      const fresh = story.estateTables.clues.filter((c) => !known.has(c.title));
      const clue = makeClue(fresh.length ? rng.pick(fresh) : rng.pick(story.estateTables.clues));
      state.clues.push(clue);
      message = `For ${BRIBE_COST} coins, a Notary clerk whispers: "${clue.title}."`;
      break;
    }
    default:
      throw new GameError('invalid', `Unknown action. Try one of: ${ACTION_TYPES.join(', ')}.`);
  }
  return { message, seasonDelta: delta };
}

export function validateLegacy(state, decisions) {
  if (!decisions || typeof decisions !== 'object' || Array.isArray(decisions)) {
    throw new GameError('invalid', 'Legacy must map item ids to keep, sell, hide or destroy.');
  }
  const ids = new Set(state.items.map((i) => i.id));
  const clean = {};
  for (const [id, choice] of Object.entries(decisions)) {
    if (!ids.has(id)) throw new GameError('not_found', `No item ${id} in this estate.`, 404);
    if (!LEGACY_CHOICES.includes(choice)) throw new GameError('invalid', `"${choice}" is not a legacy choice.`);
    clean[id] = choice;
  }
  return clean;
}

/**
 * Sunrise: turn a finished life into the estate a stranger will inherit.
 * Applies legacy choices, the family-secret box, the Notary tax and faction scoring.
 */
export function finalizeEstate(state, decisions, ctx) {
  const { story, rng, faction, lifeId, displayName, season } = ctx;
  const estate = structuredClone(state);
  const delta = {};
  const bump = (k, n = 1) => (delta[k] = (delta[k] ?? 0) + n);
  const kept = [];

  for (const item of estate.items) {
    const choice = decisions?.[item.id] ?? 'keep';
    if (choice === 'sell') {
      if (item.kind === 'ledger_page') {
        estate.money += LEDGER_SALE_PRICE;
        bump('pagesLost');
      } else {
        estate.money += Math.floor((item.value ?? 0) * SALE_RATE);
      }
      continue;
    }
    if (choice === 'destroy') {
      if (item.kind === 'ledger_page') bump('pagesLost');
      if (item.kind === 'letter' && estate.personalStory?.kind === 'unfinished_letter') {
        estate.personalStory.resolved = true;
        estate.personalStory.text = 'Your predecessor burned the letter. Whatever it said is gone.';
      }
      continue;
    }
    item.hidden = choice === 'hide';
    if (item.kind === 'box') {
      item.data.streak = (item.data.streak ?? 0) + 1;
      if (item.data.streak >= FAMILY_SECRET_HEIRS) {
        estate.money += 400;
        const page = makeLedgerPage(story, (season?.pagesFound ?? 0) + (delta.pagesFound ?? 0) + 1, rng);
        bump('pagesFound');
        kept.push(page);
        item.kind = 'trinket';
        item.name = 'Opened iron box';
        item.description = 'Five heirs kept it. It opened for the sixth: 400 coins and a page of the Ledger.';
        if (estate.personalStory?.kind === 'family_secret') estate.personalStory.resolved = true;
      }
    }
    kept.push(item);
  }
  estate.items = kept;
  estate.clues = estate.clues.filter((c) => !c.resolved);

  const pages = kept.filter((i) => i.kind === 'ledger_page').length;
  if (faction && pages) bump(`${faction}Score`, pages);

  const tax = estate.money > 0 ? Math.floor((estate.money * story.notaryTaxPercent) / 100) : 0;
  estate.money -= tax;

  if (estate.personalStory) {
    if (estate.personalStory.resolved) estate.personalStory = null;
    else estate.personalStory.heirs += 1;
  }
  estate.history = [...(estate.history ?? []), { lifeId, displayName }].slice(-HISTORY_LENGTH);
  return { estate, seasonDelta: delta, tax };
}
