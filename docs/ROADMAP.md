# Roadmap

## Done in this first version
- Full life loop: inherit → investigate → act (10 actions) → legacy → voice note → sunrise → a stranger inherits.
- Real local-sunrise deaths, the estate pool, the Notary tax, hidden items, personal storylines, the Ledger page race, Keepers vs Breakers, revealed twists, family tree.
- A 3D diorama room with dynamic lighting and a dusk-to-dawn sky, plus touch camera and tap-to-investigate.
- Android and iOS project configuration, setup scripts, packaging scripts, a server Dockerfile, and server tests.

## Done: cinematic visuals
- A cine camera (Super 35, real lenses, depth of field with rack focus, gentle handheld motion) and four cinematics: the title loop, the Opening fly-in through the window, the will reveal round the phone, and the Sunrise with the Ferryman and Mara. They're letterboxed, have title cards, and can be skipped with a tap.
- The whole of Hollow Bay built in code: the quay street, the houses and their lit windows, the pier and rocking boats, the lighthouse beam, the far shore, sea, stars, moon and gulls, plus dust, rain and mist.
- A film grade that follows the night, Lumen, virtual shadow maps and volumetric fog on desktop, and tuned mobile quality profiles.
- A synthesised ambient soundscape (no audio assets).
- The CC0 art pipeline: one command downloads photoscanned textures, furniture, props and skies from Poly Haven and imports them. The game uses them by key and falls back to simple shapes when they're missing.

## Next
1. **First compile and on-device test.** Fix whatever the compiler finds, run the art build once, and profile on a mid-range Android phone against the budget (under 150 draw calls and 150k triangles).
2. **Art polish** (see ART_PIPELINE.md): hand-picked Fab/Megascans assets for the hero props, a serif font, characters for the Ferryman and Mara, and recorded sounds layered over the synthesised ambience.
3. **Push notification an hour before sunrise** ("Your heir is waiting. Record your voice note.") via Firebase Cloud Messaging and APNs.
4. **Shareable cards:** render the inheritance reveal and the family tree to an image and open the native share sheet. These are the game's viral hooks.
5. **Voice note safety:** automatic transcription plus a profanity and abuse filter before an heir hears a note, with a report flow (reporting already exists).
6. **Player-to-player trading and sabotage** (Act 3): trade items between living players, and forge a voice note.
7. **The live finale:** a scheduled event where the Will is read aloud from players' own voice notes, with a websocket broadcast and the hidden Ferryman's Bargain ending.
8. **Season reset tooling:** archive the season, apply the winning faction's rule (a week-long life for Breakers, a hidden Founder for Keepers), start Season 2.
9. **Accounts:** sign in with Apple or Google so a player keeps their lineage across devices.
10. **Scale:** Postgres, object storage for voice notes, and rate limiting.
