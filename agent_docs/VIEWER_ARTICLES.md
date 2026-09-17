# Viewer article rendering

The browser viewer draws fighters as pre-baked 2D silhouettes (one SVG path
per animation frame from the slippilab zips in `tools/viewer/assets`). Those
silhouettes contain only the fighter body. Everything the source spawns as an
`Item_GObj` (projectiles and bone-attached accessory articles) reaches the
viewer through the 15 wire item slots and is drawn by hand-coded SVG in
`tools/viewer/slippi-viewer/src/components/viewer/Item.tsx`. Unmatched item
kinds draw nothing.

## Wire lanes per item

`MslItem` (`src/api.h`) carries kind, state (msid), owner, position, velocity,
facing, damage, life timer, spawn id and the four Slippi item bytes
`misc0..misc3` (`0xDD7`, `0xDDB`, `0xDEB`, `0xDEF`). The live adapter and the
MSLTRACE1 codec map them as:

| lane | Slippi name | used by |
| --- | --- | --- |
| misc0 | samusMissileType | Missile radius |
| misc1 | peachTurnipFace | Turnip face, Chef trajectory index |
| misc2 | isChargeShotLaunched | Shadow Ball held/thrown |
| misc3 | chargeShotChargeLevel | Samus charge shot and Shadow Ball radius (0..7) |

Before 2026-09-15 the adapter fed the charge level from misc2 (the launched
flag), so Samus's shot was always drawn at level 0 or 1. Traces written before
that date have no `misc3` field; readers treat it as 0.

Not on the wire: item JObj scale, animation frame, rotation, attach bone
transforms, item hitboxes. Shadow Ball size is therefore a proportional
approximation of the counter, and the Judge number is recovered from the
owner's motion state (SpecialS1..9 = 355..363, SpecialAirS1..9 = 364..372)
rather than the item's animation frame.

## Attached accessories keep a frozen position

Game & Watch's accessory articles (Greenhouse, Manhole, Fire, Parachute,
Turtle, Breath, Judge, Oil Panic) and the held Shadow Ball are attached to a
fighter bone with `Item_8026AB54`; their `pos` field is written once at spawn
(the bone's world position) and never updated, exactly as Slippi records it.
The renderers therefore anchor on the owner's position and facing with the
offsets measured at spawn (`melee_sim` probe, Final Destination, facing +1):

| kind | article | spawn offset (dx, dy) | drawn at |
| --- | --- | --- | --- |
| 114 | Greenhouse (jab) | (5.4, 5.2) | can at +4, puffs to +12 |
| 115 | Manhole (down tilt) | (12.4, 1.5) | cover at +12.4 |
| 116 | Fire (forward smash) | (4.8, 14.8) | torch flame at (+7, 13) |
| 117 | Parachute (neutral air) | (-0.7, 7.0) | canopy at +15 |
| 118 | Turtle (back air) | (5.6, 8.0) at shoulder | ellipse at (-9.5, 8) behind |
| 119 | Breath (up air) | (-0.5, 13.5) | puffs from +14 |
| 120 | Judge (side special) | (-0.6, 14.7) | hammer at (+8, 8), number at +17 |
| 121 | Oil Panic (release) | accessory callback | splash from +4 to +16 |
| 112 | Shadow Ball held | (-7.6, 8.9) | (+7, 9), radius 1.6 + 0.65 x level |

Projectiles use their own position: Chef sausages (122), Rescue trampoline
(124, stays on the ground while Game & Watch launches), thrown Shadow Ball
(112 states 1..9), Disable (110, 6-frame beam at head height), Din's Fire
ember (108) and burst (109, radius from distance to Zelda since the burst
scale is an unsampled f32), Peach's Bob-omb (6), Mr. Saturn (7) and Beam
Sword (12) with held states 7, 4 and 2 respectively.

## Second batch (same day)

Item renderers added for Mario's cape / Dr. Mario's sheet (83/84, hand-attached,
swept on the owner's action frame), Dr. Mario pills (49), Link and Young Link
hookshot (62/63) and Samus's grapple (96), Ness PK Fire bolt and pillar (66/67),
PK Flash and its burst (68/78), PK Thunder head and trails (69..73), bat (101)
and yoyo (102), Pikachu Thunder (81) and Thunder Jolt (89/90), Sheik's Vanish
burst (85), Yoshi's egg (87), Bowser's flame puffs (100), Peach's parasol (103),
Ice Climbers' ice block and Blizzard (106/107). Samus's charging shot draws at
the arm cannon instead of its frozen spawn point.

Tethers: the hookshot/grapple length is not on the wire, but the grab hitbox
rides the tip, so the renderer uses the owner's farthest live hitbox as the tip
and falls back to a frame envelope while no hitbox is live.

Fighter hitboxes (four per fighter on the wire) are now drawn translucently by
`Player.tsx`, which covers attacks whose visuals are effects rather than
silhouette geometry (Samus forward air, Ness PSI aerials, Sheik's burst,
Mewtwo's Confusion). Game & Watch's up-air puffs and back-air turtle anchor to
those hitboxes while they are live.

Silhouette fallbacks (`replayStore.tsx`): the slippilab zips have no
victim-side capture, throw, cargo, bury, frozen, asleep or lift animations and
the per-character maps left them blank, so those fighters vanished. They now
borrow CaptureWaitHi / DamageFlyN / DownWaitU / FuraFura / Fall / AttackS4S
silhouettes. Overlays mark buried (mound) and frozen (ice block); Fire Fox / Fire Bird,
like Game & Watch's turtle, hammer and puffs and Ness's bat and yoyo, rely on
the hitbox overlay instead of drawn effects.

Donkey Kong's cargo hold: the source only decrements the escape timer on the
victim's mash inputs (`ftCommon_GrabMash` in `ftCo_Shouldered_Anim`), so a
passive P2 in the live viewer is carried until thrown. This matches retail.

## Viewer-only lanes added 2026-09-15

`MslCoreViewerState` (not the public `MslItem`/observation contract) gained
`item_visuals[15]`: the world translation and uniform scale of each item's
rendered joint, taken from `HSD_JObjGetMtxPtr` in `msl_core_match_write_viewer`
(for Mewtwo's Shadow Ball the grandchild joint that
`itMewtwoshadowball_UnkMotion0_Anim` scales; the root JObj otherwise). Measured
on Final Destination, the charging ball sits about 5 units behind and 12 above
Mewtwo, barely moves in X/Y (its orbit is mostly depth), and grows from joint
scale 0.7 to 2.6 at full charge, matching the 6.2-unit charge hitbox at 2.4
units per scale unit. `MslCoreViewerPlayer.last_hit_element` carries
`fp->dmg.x1860_element`; the viewer shows fire/dark flames while hitstun or
hitlag remains and electric sparks during hitlag. Both travel through the live
adapter (`visualX/Y/Scale`, `lastHitElement`) and MSLTRACE1 (`visualX`,
`visualY`, `visualScale` item fields; `element` player field). The wasm
viewer digest changes with the layout; the state digest does not.

`item_visuals` also carries each item's first live hit capsule
(`hitbox_x/y/radius` from `x5D4_hitboxes`), drawn under the same toggle; this
is what shows Yoshi's landing stars, PK Fire pillar, Blizzard and Bowser's
flames hitting. Samus's charging shot uses the same grandchild-joint scale as
Mewtwo's, so it steps up as the charge builds (misc3 only carries the level
after release).

Hitbox circles are off by default. Toggle with the H key or the control-bar
button in the viewer, or the "Show hitboxes" checkbox on the live page, which
remembers the choice in localStorage; the element exposes
`setShowHitboxes(bool)`.

## Shield center and size (2026-09-15)

`write_shield` now publishes the collision center the source uses
(`lb_8000B1CC(shield bone, descriptor offset)`, the same call
`lbColl_80007BCC` makes) instead of NaN, so the viewer no longer relies on the
per-character `shieldOffset` guesses (Ness had Fox's). `joint_uniform_scale`
takes the largest column norm: Game & Watch's skeleton is flattened along one
axis (~0.03), which made his wire shield radius 0.06. Measured centers on FD:
Fox (2.6, 8.6) r 7.2, Ness (1.3, 6.2) r 7.2, Game & Watch (0.5, 6.3) r 5.7,
Samus (0.7, 10.6) r 7.4, Peach (0.3, 10.4) r 7.1. Light shields (analog trigger
below full) draw more transparent with a dashed rim.

Tethers: `item_visuals` carries the far end of the hookshot/grapple
`ItemLink` chain (`tip_x/y`, the chain end farthest from the item; links read
as the origin until their first physics pass and are skipped). That end is the
hook or beam tip while extending and the ledge grab point while hanging
(Melee tethers only latch onto ledges; wall tethers arrived in Brawl). The
renderer draws hand-to-tip from this lane, then the live grab hitbox, then a
frame envelope. The tip is the chain's `x0` link, the one the item physics
drives into walls and ledges (`it_802A5AE0`); an earlier "farthest from the
item" pick chose the hand end during hangs because the item position is
frozen at the firing point. Probe (Samus, FD, native): hanging at
(-85, -71) with the tip at (-87.1, -7.6), the ledge attach point. Link's
hookshot also hooks vertical walls (Battlefield's lip); the same lane covers it.

Shield strength: `MslCoreViewerPlayer.shield_strength` is
`fp->lightshield_amount` (1 = hard/digital, lower = lighter analog shield,
0.3 trigger dead zone already removed) scaled to a byte; the renderer fades the
bubble continuously and dashes the rim below full strength instead of guessing
from the raw trigger. Verified for grabs in Chrome; the hang was verified in the sim
(Samus below FD's left ledge) but not screenshotted.

Peach's parasol: item state 1 is open (ftGetParasolStatus 4 via the subaction
parasol command), state 2 closed (the default restored on every motion state
change: launch and fast-fall tuck). Yoshi's egg victims (YoshiEgg 277,
KirbyYoshiEgg 332) hide their silhouette and get an egg overlay; Ness's PSI
Magnet (367..370) draws a field in front of him; the bat and yoyo are not
drawn.

## Player overlays

`Player.tsx` adds two overlays keyed on motion state: drifting "z" glyphs for
DamageSong/DamageSongWait (297/298) and Jigglypuff's Rest (369..372), and a
burst behind Luigi during SpecialSMisfire/SpecialAirSMisfire (348/354), which
share the SpecialS silhouette with the ordinary launch.

## Known gaps (not items)

- Fox's and Falco's side special hit lands in the sim (7% on a passive Fox)
  but no fighter or item hit capsule is enabled in the end-of-frame viewer
  projection during SpecialS (348); the capsule owner/lifetime still needs
  tracing before it can be drawn.
- The live page resets the whole match whenever any fighter is dead
  (`frameHasDeadPlayer` in `tools/viewer/live/main.js`), which is the
  "teleport" of the controlled fighter on the opponent's death. Ordinary
  respawn works in the sim; removing that reset is a one-line change.

- Ganondorf's and Marth's airborne capes and the sliver by Donkey Kong's mouth
  in his idle are baked into the slippilab silhouettes.
- Game & Watch's back-air turtle, up-air puffs and Judge hammer are no longer
  drawn; their hits are on the fighter and show through the hitbox overlay.
  The Judge number remains.

- Game & Watch's forward tilt chair, forward air box, down air key, up tilt
  flag, down smash hammers, dash attack helmet and the Oil Panic bucket are
  mesh visibility toggles on the fighter model, not articles. The slippilab
  bake hid them, so they need a silhouette re-bake with per-move part
  visibility.
- Mewtwo's Confusion spawns no item; its silhouette exists and animates.
- Peach's idle silhouettes (Wait1..4) contain a baked cone above her head on
  the arm-raise frames. This is in the asset, not the renderer.
- Held Shadow Ball drifts around Mewtwo's hand in the game; the wire has no
  bone transform, so it is drawn at a fixed hand offset.
