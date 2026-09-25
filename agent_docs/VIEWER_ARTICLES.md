# Viewer effects and state rendering

The browser draws fighter silhouettes from the existing Slippi Lab assets and
move effects as SVG in `Item.tsx` and `Player.tsx`. These effects are visual
approximations; the simulator owns gameplay, collisions and damage.

## Runtime projection

`src/runtime/viewer.c` publishes viewer-only fields from existing game state.
The public observation and item layouts are unchanged by these additions.
`tools/viewer/schema.c` generates the offsets consumed by the live adapter;
MSLTRACE1 preserves the same optional values for exported recordings.

- `item_visuals` parallels the existing 15 item slots in entity-list order.
  It carries the rendered joint's world position and largest-axis scale,
  the first enabled item hit capsule, and a tether tip when available.
- Held Samus Charge Shot and Mewtwo Shadow Ball (including Kirby copies) use
  the animated grandchild joint. Their root positions stay at the spawn point;
  the grandchild follows the charging animation and its scale.
- Link/Young Link hookshot and Samus grapple use the `ItemLink.x0` endpoint.
  It is the chain endpoint driven by item physics, including the hang anchor.
  Zeroed links before the first physics pass are not published as valid tips.
- Shield centers use `lb_8000B1CC(shield->bone, shield->offset)`, matching
  `lbColl_80007BCC`. Radius uses the largest matrix-column norm because
  Game & Watch's flattened skeleton makes one axis unsuitable as a scale.
- `shield_strength` publishes `fp->lightshield_amount` as a byte. Lower
  strength draws a lighter bubble with a dashed rim.
- `last_hit_element` comes from `fp->dmg.x1860_element`. Fire, electric and
  dark-hit overlays appear during hitlag/hitstun. Pichu self-damage has no
  incoming hit element; the viewer infers a short electric cue from a percent
  increase outside hitlag/hitstun.
- `bucket_fill` publishes Game & Watch's `fv.gw.x2238_panicCharge` (0–3).
  Oil Panic's damage belongs to fighter hitboxes; the release art follows
  those hitboxes, rather than assigning a hitbox to the visual item.

The item byte mapping is `misc0` = missile type, `misc1` = turnip face,
`misc2` = charge-shot launched flag, `misc3` = charge level. Older traces
without the optional visual fields retain position/charge-based rendering.
The original v1 exporter used `misc2` for charge level; readers preserve that
interpretation when `misc3` is absent.
See `tools/viewer/TRACE_FORMAT.md` for field names and defaults.

## Browser rendering

Item effects include Bowser flame puffs; Pikachu/Pichu Thunder and Thunder
Jolt; Ness PK Fire/Flash/Thunder; Mario's cape and Dr. Mario's sheet/pills;
Ice Climbers ice/Blizzard; Zelda Din's Fire; Sheik Vanish; Yoshi eggs/stars;
Peach parasol, turnip faces and rare pulls; Mewtwo Disable/Shadow Ball;
Game & Watch articles; and Link/Young Link/Samus tethers.

Game & Watch's parachute, turtle and up-air puffs follow the item's visual
joint, which follows the fighter bone (`Item_8026AB54`). Judge's number is
recovered from the owner's SpecialS1–9 / SpecialAirS1–9 motion state.

Missing capture, throw, cargo, buried, frozen, sleeping and lifted silhouettes
use available similar poses at the fighter's actual position. Buried, frozen,
sleeping, Yoshi-egg and Luigi-misfire overlays distinguish those states. An
unmapped character-specific action name remains a string so it cannot stop
the render loop through an undefined `.includes()` call.

Fighter hitboxes and the first enabled item hitbox are off by default. Use
H or the control-bar button in the replay viewer; the live page has a persisted
"Show hitboxes" checkbox. Embedders can call `setShowHitboxes(boolean)`.

## Limits

This packet does not replace silhouette assets. Existing asset omissions and
artifacts remain, including Game & Watch's Judge hammer and Ness's bat/yoyo.
Hitbox overlays show their hits where the end-of-frame projection has them.
Some transient hitboxes (such as Fox/Falco side special) are absent by that
projection phase. Ordinary respawn exists in the sim, but the live page still
resets the whole match when its death gate fires.
