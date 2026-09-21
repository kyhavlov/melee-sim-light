import { Character } from "~/viewer/characters/character";
import { ActionName } from "~/common/ids";

export const kirby: Character = {
  scale: 0.92,
  shieldOffset: [0, 4.828],
  shieldSize: 0.92 * 14.7,
  animationMap: new Map<ActionName, string>([
    ["AppealL", "AppealL"],
    ["AppealR", "AppealR"],
    ["AttackS3Hi", "AttackS3Hi"],
    ["AttackS3HiS", "AttackS3Hi"],
    ["AttackS3Lw", "AttackS3Lw"],
    ["AttackS3LwS", "AttackS3Lw"],
    ["AttackS3S", "AttackS3S"],
    ["AttackS4Hi", "AttackS4Hi"],
    ["AttackS4HiS", "AttackS4Hi"],
    ["AttackS4Lw", "AttackS4Lw"],
    ["AttackS4LwS", "AttackS4Lw"],
    ["AttackS4S", "AttackS4S"],
    ["BarrelWait", ""],
    ["Bury", ""],
    ["BuryJump", ""],
    ["BuryWait", ""],
    ["CaptureCaptain", ""],
    ["CaptureDamageKoopa", ""],
    ["CaptureDamageKoopaAir", ""],
    ["CaptureKirby", ""],
    ["CaptureKirbyYoshi", ""],
    ["CaptureKoopa", ""],
    ["CaptureKoopaAir", ""],
    ["CaptureMewtwo", ""],
    ["CaptureMewtwoAir", ""],
    ["CaptureWaitKirby", ""],
    ["CaptureWaitKoopa", ""],
    ["CaptureWaitKoopaAir", ""],
    ["CaptureYoshi", ""],
    ["CatchDashPull", "CatchWait"],
    ["CatchPull", "CatchWait"],
    ["DamageBind", ""],
    ["DamageIce", ""],
    ["DamageIceJump", "Fall"],
    ["DamageSong", ""],
    ["DamageSongRv", ""],
    ["DamageSongWait", ""],
    ["DeadDown", ""],
    ["DeadLeft", ""],
    ["DeadRight", ""],
    ["DeadUpFallHitCamera", ""],
    ["DeadUpFallHitCameraIce", ""],
    ["DeadUpFallIce", ""],
    ["DeadUpStar", ""],
    ["DeadUpStarIce", ""],
    ["DownReflect", ""],
    ["EntryEnd", "Entry"],
    ["EntryStart", "Entry"],
    ["Escape", "EscapeN"],
    ["FlyReflectCeil", ""],
    ["FlyReflectWall", "WallDamage"],
    ["Guard", "Guard"],
    ["GuardOff", "GuardOff"],
    ["GuardOn", "GuardOn"],
    ["GuardReflect", "Guard"],
    ["GuardSetOff", "GuardDamage"],
    ["ItemParasolDamageFall", ""],
    ["ItemParasolFall", ""],
    ["ItemParasolFallSpecial", ""],
    ["ItemParasolOpen", ""],
    ["KirbyYoshiEgg", ""],
    ["KneeBend", "Landing"],
    ["LandingFallSpecial", "Landing"],
    ["LiftTurn", ""],
    ["LiftWait", ""],
    ["LiftWalk1", ""],
    ["LiftWalk2", ""],
    ["LightThrowAirB4", "LightThrowAirB"],
    ["LightThrowAirF4", "LightThrowAirF"],
    ["LightThrowAirHi4", "LightThrowAirHi"],
    ["LightThrowAirLw4", "LightThrowAirLw"],
    ["LightThrowB4", "LightThrowB"],
    ["LightThrowF4", "LightThrowF"],
    ["LightThrowHi4", "LightThrowHi"],
    ["LightThrowLw4", "LightThrowLw"],
    ["Rebirth", "Entry"],
    ["RebirthWait", "Wait1"],
    ["SquatWait", "SquatWait1"],
    ["ReboundStop", "Rebound"],
    ["RunDirect", ""],
    ["ShieldBreakDownD", "DownBoundD"],
    ["ShieldBreakDownU", "DownBoundU"],
    ["ShieldBreakFall", "DamageFall"],
    ["ShieldBreakFly", ""],
    ["ShieldBreakStandD", "DownStandD"],
    ["ShieldBreakStandU", "DownStandU"],
    ["ShoulderedTurn", ""],
    ["ShoulderedWait", ""],
    ["ShoulderedWalkFast", ""],
    ["ShoulderedWalkMiddle", ""],
    ["ShoulderedWalkSlow", ""],
    ["SwordSwing1", "Swing1"],
    ["SwordSwing3", "Swing3"],
    ["SwordSwing4", "Swing4"],
    ["SwordSwingDash", "SwingDash"],
    ["ThrownB", ""],
    ["ThrownCopyStar", ""],
    ["ThrownF", ""],
    ["ThrownFB", ""],
    ["ThrownFF", ""],
    ["ThrownFHi", ""],
    ["ThrownFLw", ""],
    ["ThrownHi", ""],
    ["ThrownKirby", ""],
    ["ThrownKirbyStar", ""],
    ["ThrownKoopaAirB", ""],
    ["ThrownKoopaAirF", ""],
    ["ThrownKoopaB", ""],
    ["ThrownKoopaF", ""],
    ["ThrownLw", ""],
    ["ThrownLwWomen", ""],
    ["ThrownMewtwo", ""],
    ["ThrownMewtwoAir", ""],
    ["Wait", "Wait1"],
    ["YoshiEgg", ""],
  ]),
  specialsMap: new Map<number, string>([
    [341, "JumpAerialF1"],
    [342, "JumpAerialF2"],
    [343, "JumpAerialF3"],
    [344, "JumpAerialF4"],
    [345, "JumpAerialF5"],
    [346, "JumpAerialF1Met"],
    [347, "JumpAerialF2Met"],
    [348, "JumpAerialF3Met"],
    [349, "JumpAerialF4Met"],
    [350, "JumpAerialF5Met"],
    [351, "AttackDash"],
    [352, "AttackDash"],
    [353, "SpecialN"], // Ground Startup (Drink?)
    [354, "SpecialNLoop"],
    [355, "SpecialNEnd"],
    [356, "Eat"], // Capture (Eat?)
    [357, ""], // ???
    [358, ""], // Captured (used as swallowed character state? or is that ThrownKirby?)
    [359, "EatWait"],
    [360, "EatWalkSlow"],
    [361, "EatWalkMiddle"],
    [362, "EatWalkFast"],
    [363, "EatTurn"],
    [364, "EatLanding"],
    [365, "EatJump1"],
    [366, "EatLanding"],
    [367, "SpecialNDrink"], // Digest (Drink?)
    [368, ""], // ???
    [369, "SpecialNSpit"], // Spit
    [370, ""], // ???
    [371, "SpecialN"], // Air Startup (Drink?)
    [372, "SpecialNLoop"],
    [373, "SpecialNEnd"],
    [374, "Eat"], // Air Capture (Eat?)
    [375, ""], // ???
    [376, ""], // Air Captured (see 358)
    [377, "EatWait"],
    [378, "SpecialNDrink"], // Air Digest (Drink?)
    [379, ""], // ???
    [380, "SpecialNSpit"], // Air Spit
    [381, ""], // ???
    [382, "EatTurn"],
    [383, "SpecialS"],
    [384, "SpecialAirS"],
    [385, "SpecialHi1"],
    [386, "SpecialHi2"],
    [387, "SpecialHi3"],
    [388, "SpecialHi4"],
    [389, "SpecialAirHi1"],
    [390, "SpecialAirHi2"],
    [391, "SpecialAirHi3"],
    [392, "SpecialAirHi4"],
    [393, "SpecialLw1"],
    [394, "SpecialLw1"],
    [395, "SpecialLw2"],
    [396, "SpecialAirLw1"],
    [397, "SpecialAirLw1"],
    [398, "SpecialAirLw2"],
    // 399 - 543 are the copy-ability neutral specials, in ftKb_MotionState
    // order. Names map to the Slippi Lab kirby.zip where it ships the hat
    // animation; the rest fall back to the plain inhale pose so Kirby stays
    // visible.
    [399, "MrSpecialN"],
    [400, "MrSpecialAirN"],
    [401, "LkSpecialNStart"],
    [402, "LkSpecialNLoop"],
    [403, "LkSpecialNEnd"],
    [404, "LkSpecialAirNStart"],
    [405, "LkSpecialAirNLoop"],
    [406, "LkSpecialAirNEnd"],
    [407, "SsSpecialNStart"],
    [408, "SsSpecialNHold"],
    [409, "SsSpecialNCancel"],
    [410, "SsSpecialN"],
    [411, "SsSpecialAirNStart"],
    [412, "SsSpecialAirN"],
    [413, "YsSpecialN1"],
    [414, "SpecialN"], // YsSpecialNCapture1_0 (no asset)
    [415, "SpecialN"], // YsSpecialNCapture1_1 (no asset)
    [416, "SpecialN"], // YsSpecialNCapture2_0 (no asset)
    [417, "SpecialN"], // YsSpecialNCapture2_1 (no asset)
    [418, "SpecialN"], // YsSpecialAirNCapture2 (no asset)
    [419, "SpecialN"], // YsSpecialAirNCapture1_0 (no asset)
    [420, "SpecialN"], // YsSpecialAirNCapture1_1 (no asset)
    [421, "YsSpecialAirN2"], // YsSpecialAirN2_0
    [422, "YsSpecialAirN2"], // YsSpecialAirN2_1
    [423, "FxSpecialNStart"],
    [424, "FxSpecialNLoop"],
    [425, "FxSpecialNEnd"],
    [426, "FxSpecialAirNStart"],
    [427, "FxSpecialAirNLoop"],
    [428, "FxSpecialAirNEnd"],
    [429, "PkSpecialN"],
    [430, "PkSpecialAirN"],
    [431, "LgSpecialN"],
    [432, "LgSpecialAirN"],
    [433, "CaSpecialN"],
    [434, "CaSpecialAirN"],
    [435, "NsSpecialNStart"],
    [436, "NsSpecialNHold"], // NsSpecialNHold0
    [437, "NsSpecialNHold"], // NsSpecialNHold1
    [438, "NsSpecialNEnd"],
    [439, "NsSpecialAirNStart"],
    [440, "NsSpecialAirNHold"], // NsSpecialAirNHold0
    [441, "NsSpecialAirNHold"], // NsSpecialAirNHold1
    [442, "NsSpecialAirNEnd"],
    [443, "KpSpecialNStart"],
    [444, "KpSpecialN"],
    [445, "KpSpecialNEnd"],
    [446, "KpSpecialAirNStart"],
    [447, "KpSpecialAirN"],
    [448, "KpSpecialAirNEnd"],
    [449, "PeSpecialLw"],
    [450, "PeSpecialLwHit"],
    [451, "PeSpecialAirLw"],
    [452, "PeSpecialAirLwHit"],
    [453, "PpSpecialN"],
    [454, "PpSpecialAirN"],
    [455, "DkSpecialNStart"],
    [456, "DkSpecialNLoop"],
    [457, "DkSpecialNCansel"], // DkSpecialNCancel
    [458, "DkSpecialN"],
    [459, "SpecialN"], // DkSpecialNFull (no asset)
    [460, "DkSpecialAirNStart"],
    [461, "DkSpecialAirNLoop"],
    [462, "DkSpecialAirNCancel"],
    [463, "DkSpecialAirN"],
    [464, "SpecialN"], // DkSpecialAirNFull (no asset)
    [465, "ZdSpecialN"],
    [466, "ZdSpecialAirN"],
    [467, "SkSpecialNStart"],
    [468, "SkSpecialNLoop"],
    [469, "SkSpecialNCancel"],
    [470, "SkSpecialNEnd"],
    [471, "SkSpecialAirNStart"],
    [472, "SkSpecialAirNLoop"],
    [473, "SkSpecialAirNCancel"],
    [474, "SkSpecialAirNEnd"],
    [475, "SpecialN"], // PrSpecialNStartR (no asset)
    [476, "SpecialN"], // PrSpecialNStartL (no asset)
    [477, "SpecialN"], // PrSpecialNLoop (no asset)
    [478, "SpecialN"], // PrSpecialNFull (no asset)
    [479, "SpecialN"], // PrSpecialN1 (no asset)
    [480, "SpecialN"], // PrSpecialNTurn (no asset)
    [481, "SpecialN"], // PrSpecialNEndR (no asset)
    [482, "SpecialN"], // PrSpecialNEndL (no asset)
    [483, "SpecialN"], // PrSpecialAirNStartR (no asset)
    [484, "SpecialN"], // PrSpecialAirNStartL (no asset)
    [485, "SpecialN"], // PrSpecialAirNLoop (no asset)
    [486, "SpecialN"], // PrSpecialAirNFull (no asset)
    [487, "SpecialN"], // PrSpecialAirN (no asset)
    [488, "SpecialN"], // PrSpecialN0 (no asset)
    [489, "SpecialN"], // PrSpecialAirNEndR0 (no asset)
    [490, "SpecialN"], // PrSpecialAirNEndR1 (no asset)
    [491, "SpecialN"], // PrSpecialNHit (no asset)
    [492, "MsSpecialNStart"],
    [493, "MsSpecialNLoop"],
    [494, "MsSpecialNEnd"], // MsSpecialNEnd0
    [495, "MsSpecialNEnd"], // MsSpecialNEnd1
    [496, "MsSpecialAirNStart"],
    [497, "MsSpecialAirNLoop"],
    [498, "MsSpecialAirNEnd"], // MsSpecialAirNEnd0
    [499, "MsSpecialAirNEnd"], // MsSpecialAirNEnd1
    [500, "SpecialN"], // MtSpecialNStart (no asset)
    [501, "SpecialN"], // MtSpecialNLoop (no asset)
    [502, "SpecialN"], // MtSpecialNLoopFull (no asset)
    [503, "SpecialN"], // MtSpecialNCancel (no asset)
    [504, "SpecialN"], // MtSpecialNEnd (no asset)
    [505, "SpecialN"], // MtSpecialAirNStart (no asset)
    [506, "SpecialN"], // MtSpecialAirNLoop (no asset)
    [507, "SpecialN"], // MtSpecialAirNLoopFull (no asset)
    [508, "SpecialN"], // MtSpecialAirNCancel (no asset)
    [509, "SpecialN"], // MtSpecialAirNEnd (no asset)
    [510, "GwSpecialN"],
    [511, "GwSpecialAirN"],
    [512, "SpecialN"], // DrSpecialN (no asset)
    [513, "SpecialN"], // DrSpecialAirN (no asset)
    [514, "SpecialN"], // ClSpecialNStart (no asset)
    [515, "SpecialN"], // ClSpecialNLoop (no asset)
    [516, "SpecialN"], // ClSpecialNEnd (no asset)
    [517, "SpecialN"], // ClSpecialAirNStart (no asset)
    [518, "SpecialN"], // ClSpecialAirNLoop (no asset)
    [519, "SpecialN"], // ClSpecialAirNEnd (no asset)
    [520, "FcSpecialNStart"],
    [521, "FcSpecialNLoop"],
    [522, "FcSpecialNEnd"],
    [523, "FcSpecialAirNStart"],
    [524, "FcSpecialAirNLoop"],
    [525, "FcSpecialAirNEnd"],
    [526, "SpecialN"], // PcSpecialN (no asset)
    [527, "SpecialN"], // PcSpecialAirN (no asset)
    [528, "GnSpecialN"],
    [529, "GnSpecialAirN"],
    [530, "SpecialN"], // FeSpecialNStart (no asset)
    [531, "SpecialN"], // FeSpecialNLoop (no asset)
    [532, "SpecialN"], // FeSpecialNEnd0 (no asset)
    [533, "SpecialN"], // FeSpecialNEnd1 (no asset)
    [534, "SpecialN"], // FeSpecialAirNStart (no asset)
    [535, "SpecialN"], // FeSpecialAirNLoop (no asset)
    [536, "SpecialN"], // FeSpecialAirNEnd0 (no asset)
    [537, "SpecialN"], // FeSpecialAirNEnd1 (no asset)
    [538, "SpecialN"], // GkSpecialNStart (no asset)
    [539, "SpecialN"], // GkSpecialN (no asset)
    [540, "SpecialN"], // GkSpecialNEnd (no asset)
    [541, "SpecialN"], // GkSpecialAirNStart (no asset)
    [542, "SpecialN"], // GkSpecialAirN (no asset)
    [543, "SpecialN"], // GkSpecialAirNEnd (no asset)
  ]),
};
