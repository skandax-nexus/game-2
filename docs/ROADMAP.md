# Roadmap

## Done in this first version
- Full life loop: inherit → investigate → act (10 actions) → legacy → voice note → sunrise → a stranger inherits.
- Real local-sunrise deaths, the estate pool, the Notary tax, hidden items, personal storylines, the Ledger page race, Keepers vs Breakers, revealed twists, family tree.
- A 3D diorama room with dynamic lighting and a dusk-to-dawn sky, plus touch camera and tap-to-investigate.
- Android and iOS project configuration, setup scripts, packaging scripts, a server Dockerfile, and server tests.

## Next
1. **First compile and on-device test.** Fix whatever the compiler finds and profile on a mid-range Android phone.
2. **Art pass** (see ART_PIPELINE.md): a hand-dressed Hollow Bay level, a serif font, an ambient soundscape.
3. **Push notification an hour before sunrise** ("Your heir is waiting. Record your voice note.") via Firebase Cloud Messaging and APNs.
4. **Shareable cards:** render the inheritance reveal and the family tree to an image and open the native share sheet. These are the game's viral hooks.
5. **Voice note safety:** automatic transcription plus a profanity and abuse filter before an heir hears a note, with a report flow (reporting already exists).
6. **Player-to-player trading and sabotage** (Act 3): trade items between living players, and forge a voice note.
7. **The live finale:** a scheduled event where the Will is read aloud from players' own voice notes, with a websocket broadcast and the hidden Ferryman's Bargain ending.
8. **Season reset tooling:** archive the season, apply the winning faction's rule (a week-long life for Breakers, a hidden Founder for Keepers), start Season 2.
9. **Accounts:** sign in with Apple or Google so a player keeps their lineage across devices.
10. **Scale:** Postgres, object storage for voice notes, and rate limiting.
