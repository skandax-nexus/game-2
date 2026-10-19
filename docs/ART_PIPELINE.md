# Making it look great

The game runs with real UE5 lighting (sky atmosphere, volumetric fog, dynamic shadows, bloom, film grain, colour grading, a dusk-to-dawn sky tied to your remaining life), but every object is an engine basic shape. This is how to replace them with high-quality art without changing any game logic.

## Where the geometry comes from

| What | Code |
| --- | --- |
| Floor, walls, window, furniture, bed, lamp | `AInhRoomBuilder::BuildShell()` |
| Harbour, rooftops, lighthouse | `AInhRoomBuilder::BuildOutside()` |
| Phone, letters, Ledger pages, the iron box, trinkets | `AInhRoomBuilder::ShowLife()` / `SpawnItem()` |
| Debt notices, feud portraits, clue cards | `AInhRoomBuilder::ShowLife()` |

Each part is one `InhShapes::AddPart(…, Mesh, Location, Size, Color)` call. To swap one, pass your mesh instead of `InhShapes::Cube()`. Use a size of `FVector(100)` for a mesh authored at real scale, and give it its own material.

## Recommended path (free assets)

1. **Fab** (Epic's marketplace, built into UE 5.4+: *Window → Fab*). Search for "Victorian interior", "fishing village", "lighthouse" or "props". Many are free with Quixel Megascans and are already optimised for mobile LODs.
2. Create `Game/Content/Art/` and import there. Check *Nanite* off for mobile meshes, and generate LODs.
3. **Best long-term option:** build the room as a level instead of in code. Make `Content/Maps/HollowBay.umap`, dress it with the imported art, place an `InhRoomBuilder` in it, and set it as the default map. Then strip `BuildShell()` down to just the item slots. The props and gameplay keep working.
4. **Characters** (the Ferryman, Mara at the waterline, Judge Thrane): use **MetaHuman Creator** with the *mobile* LOD set.
5. **Fonts:** import a serif such as *Cormorant* or *IM Fell* (both open-source on Google Fonts) as a Font asset and use it in `InhStyle::Font()`.
6. **Audio:** harbour ambience, gulls, a fog horn and a sunrise bell as SoundCues. The voice notes already play as 2D voice sounds.

## Mobile budgets

- Under 100k triangles on screen, under 150 draw calls (merge static props with *Merge Actors*).
- Textures at most 2048 px, ASTC on Android, with mipmaps.
- At most 4 dynamic shadow-casting lights. The lamp and moon cast shadows; prop markers don't.
- Volumetric fog only runs on high-end/desktop. Mobile falls back to height fog automatically.
