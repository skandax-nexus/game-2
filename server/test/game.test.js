import test from 'node:test';
import assert from 'node:assert/strict';
import { loadStory } from '../src/story.js';
import { createRng } from '../src/rng.js';
import { applyAction, finalizeEstate, generateStarterEstate, GameError, validateLegacy } from '../src/game.js';

const story = loadStory();
const act = story.acts[0];
const ctx = (seed = 'x') => ({ story, act, rng: createRng(seed), season: {} });

test('starter estates have the pieces a life needs', () => {
  for (let i = 0; i < 50; i++) {
    const e = generateStarterEstate(story, createRng(i));
    assert.ok(e.money >= 50 && e.money <= 300);
    assert.ok(e.debts.length >= 1);
    assert.ok(e.items.length >= 2);
    assert.ok(e.clues.length >= 1);
    assert.ok(e.house.name);
  }
});

test('paying a debt needs the money', () => {
  const s = { money: 100, debts: [{ id: 'd', creditor: 'Pike', amount: 150 }], items: [], feuds: [], clues: [] };
  assert.throws(() => applyAction(s, { type: 'pay_debt', debtId: 'd' }, ctx()), (e) => e.code === 'not_enough_money');
  s.money = 200;
  applyAction(s, { type: 'pay_debt', debtId: 'd' }, ctx());
  assert.equal(s.money, 50);
  assert.equal(s.debts.length, 0);
});

test('dodging a debt grows it', () => {
  const s = { money: 0, debts: [{ id: 'd', creditor: 'Pike', amount: 100 }], items: [], feuds: [], clues: [] };
  applyAction(s, { type: 'dodge_debt', debtId: 'd' }, ctx());
  assert.equal(s.debts[0].amount, 150);
});

test('chasing clues eventually finds ledger pages and counts them for the season', () => {
  let found = 0;
  for (let i = 0; i < 40; i++) {
    const s = { money: 0, debts: [], items: [], feuds: [], clues: [{ id: 'c', title: 'T', text: '', resolved: false }] };
    const { seasonDelta } = applyAction(s, { type: 'chase_clue', clueId: 'c' }, ctx(`seed${i}`));
    if (seasonDelta.pagesFound) {
      found++;
      assert.equal(s.items[0].kind, 'ledger_page');
    }
  }
  assert.ok(found > 3 && found < 25, `found ${found}`);
});

test('forgiving a blood feud ends the revenge chain for free', () => {
  const s = {
    money: 0,
    debts: [],
    items: [],
    clues: [],
    feuds: [{ id: 'f', rival: 'heir', reason: 'r', severity: 'blood' }],
    personalStory: { kind: 'revenge_chain', resolved: false, heirs: 2 },
  };
  applyAction(s, { type: 'settle_feud', feudId: 'f' }, ctx());
  assert.equal(s.feuds.length, 0);
  assert.equal(s.personalStory.resolved, true);
});

test('unknown actions and legacy choices are rejected', () => {
  const s = { money: 0, debts: [], items: [{ id: 'i', kind: 'trinket' }], feuds: [], clues: [] };
  assert.throws(() => applyAction(s, { type: 'fly' }, ctx()), GameError);
  assert.throws(() => validateLegacy(s, { i: 'eat' }), GameError);
  assert.throws(() => validateLegacy(s, { nope: 'keep' }), GameError);
  assert.deepEqual(validateLegacy(s, { i: 'hide' }), { i: 'hide' });
});

test('sunrise applies legacy choices, Notary tax, and faction score', () => {
  const state = {
    money: 100,
    debts: [],
    feuds: [],
    clues: [{ id: 'c1', resolved: true }, { id: 'c2', resolved: false }],
    items: [
      { id: 'a', kind: 'trinket', value: 50, data: {} },
      { id: 'b', kind: 'trinket', value: 10, data: {} },
      { id: 'p', kind: 'ledger_page', value: 0, data: {} },
      { id: 'h', kind: 'trinket', value: 10, data: {} },
    ],
    personalStory: null,
    history: [],
  };
  const { estate, seasonDelta, tax } = finalizeEstate(
    state,
    { a: 'sell', b: 'destroy', h: 'hide' },
    { story, rng: createRng(1), faction: 'breakers', lifeId: 'L', displayName: 'Ana', season: {} },
  );
  assert.equal(tax, 14); // 10% of 100 + 40 from the sale
  assert.equal(estate.money, 126);
  assert.deepEqual(estate.items.map((i) => i.id), ['p', 'h']);
  assert.equal(estate.items[1].hidden, true);
  assert.deepEqual(estate.clues.map((c) => c.id), ['c2']);
  assert.equal(seasonDelta.breakersScore, 1);
  assert.deepEqual(estate.history, [{ lifeId: 'L', displayName: 'Ana' }]);
});

test('the family secret box opens after five heirs keep it', () => {
  let estate = {
    money: 0,
    debts: [],
    feuds: [],
    clues: [],
    items: [{ id: 'box', kind: 'box', name: 'Locked iron box', value: 0, data: { streak: 0 } }],
    personalStory: { kind: 'family_secret', resolved: false, heirs: 0 },
    history: [],
  };
  for (let i = 0; i < 5; i++) {
    ({ estate } = finalizeEstate(estate, {}, { story, rng: createRng(i), lifeId: `L${i}`, displayName: 'x', season: {} }));
  }
  assert.ok(estate.items.some((i) => i.kind === 'ledger_page'));
  assert.equal(estate.money, 360); // 400 minus the Notary's 10%
  assert.equal(estate.personalStory, null);
});
