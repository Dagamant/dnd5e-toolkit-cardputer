#pragma once
// Built-in tables for the 5-Room Dungeon generator.
// Override any table by putting a text file (one entry per line) at
//   /gmtools/dungeon/<table>.txt
// Table names: adjective place hook entrance guardian puzzle trick boss terrain reward twist

namespace dtab {

struct Table { const char* key; const char* const* items; int count; };
#define DTAB(name, ...) static const char* const name[] = {__VA_ARGS__};

namespace fantasy {
DTAB(adjective, "Drowned", "Sunken", "Forgotten", "Burning", "Hollow", "Weeping", "Shattered", "Silent", "Gilded",
     "Rotting", "Frozen", "Crooked", "Starless", "Bleeding", "Whispering", "Ashen")
DTAB(place, "Chapel", "Barrow", "Lighthouse", "Monastery", "Mine", "Observatory", "Prison", "Wizard's Tower",
     "Sewer Vault", "Tomb", "Dwarven Hall", "Witch's Mill", "Library", "Temple", "Smugglers' Cave", "Manor")
DTAB(hook,
     "A dying courier carries a map to this place, sealed with the royal crest.",
     "Villagers have gone missing, one every new moon.",
     "A scholar pays well for a rubbing of an inscription deep inside.",
     "The party's patron needs a relic recovered before a rival gets it.",
     "Something below is poisoning the town's well.",
     "A ghost begs the party to return its stolen bones.",
     "The party owes a favour, and this job is the price.",
     "Strange lights glow here at night. The local lord wants answers.",
     "A child dared the party to go in, then vanished inside.",
     "Bandits fled here with the temple's tithe.",
     "An earthquake has opened the way for the first time in centuries.",
     "A rival adventuring party went in three days ago and never came out.")
DTAB(entrance,
     "A collapsed stair. The only way in is a narrow chimney.",
     "A door sealed with a riddle written in a dead language.",
     "A flooded entry: thirty feet swum underwater on one breath.",
     "A portcullis jammed half-open, with the winch room above.",
     "An illusory wall, found by following draughts or vermin.",
     "A bridge over a chasm, its planks rotted in places.",
     "Stone doors that open only under moonlight or when a bell tolls.",
     "A thorn hedge grown into a maze over the gate.",
     "A pressure plate on the threshold that fires poison darts.",
     "A carved stone mouth that demands a toll to pass.")
DTAB(guardian,
     "A stone golem that challenges anyone without the sigil.",
     "A pack of starving ghouls lairing in the antechamber.",
     "A territorial owlbear nesting in the ruins.",
     "Suits of animated armour, posted at the gate centuries ago.",
     "A bored ogre who trades passage for food or a good story.",
     "Goblin sentries with a gong to warn their friends.",
     "A swarm of bats or rats that boils out when disturbed.",
     "A spectral knight who duels the strongest intruder.",
     "Giant spiders whose webs fill the vaulted ceiling.",
     "Cultists posing as pilgrims, all smiles and lies.",
     "A gelatinous cube patrolling a suspiciously clean hallway.",
     "A chained basilisk whose keeper died long ago.")
DTAB(puzzle,
     "Statues must be turned to face the one that looks away.",
     "A ghost helps only if the party works out how it died.",
     "A floor of rune tiles. Stepping out of sequence brings fire.",
     "A prisoner knows the way out but lies about who they are.",
     "Scales that must balance against exactly the right offering.",
     "A talking door that loves compliments and hates lies.",
     "A rival faction is camped here. Negotiate, bribe or bluff past.",
     "A mural is missing one piece, and the party has seen it elsewhere.",
     "Water must be diverted through channels to open a sluice gate.",
     "A riddling skull with a cruel sense of humour.",
     "A mirror shows a different room, and only a reflection can pass through.",
     "A sleeping wyrmling. Sneak past it, or wake it and talk.")
DTAB(trick,
     "The treasure chest is a mimic, and it bites.",
     "The floor tilts and drops the party to a lower level.",
     "A trusted guide betrays the party and flees with the light.",
     "A cave-in seals the way back. There is no retreat.",
     "The exit is a portal that splits the party for 1d4 rounds.",
     "A cursed fountain: whoever drinks gains a boon and a doom.",
     "Rising water floods the room over 10 rounds.",
     "The treasure is an illusion. The real hoard is behind a false wall.",
     "The friendly NPC from the last room is the villain's spy.",
     "A trap drains magic. Spells fizzle until the next long rest.",
     "Spores make allies look like enemies.",
     "The boss's minions ambush the party during a short rest.")
DTAB(boss,
     "A necromancer in the middle of a ritual, surrounded by the fresh dead.",
     "A young dragon guarding its first hoard.",
     "A cult leader who becomes their god's vessel at half HP.",
     "A coven of three hags, each with a different trick.",
     "A vampire spawn who secretly wants freedom from its master.",
     "An aberration that whispers inside every mind.",
     "A fallen paladin, still sure of their own righteousness.",
     "A hulking troll who uses the furnaces of a forge as weapons.",
     "A medusa in a garden of statues, some of them adventurers.",
     "A floating eye tyrant at the bottom of a vertical shaft.",
     "A bandit queen with elite guards and an escape plan.",
     "An awakened tree whose roots run through the whole dungeon.")
DTAB(terrain,
     "on a crumbling bridge above a lava flow",
     "in a flooded hall, waist-deep in black water",
     "among swinging chains and moving platforms",
     "on a floor that tilts every round",
     "in total darkness lit only by the ritual circle",
     "among tall pillars, perfect for cover and ambushes",
     "across balconies and ladders on several levels",
     "in a room slowly filling with poison gas",
     "around a magic circle that empowers whoever stands in it",
     "among stacked barrels of lamp oil")
DTAB(reward,
     "The relic they came for, intact and humming.",
     "A spellbook holding a spell lost for centuries.",
     "A chest of ancient coins bearing a forgotten royal mark.",
     "A magic weapon that whispers its old owner's name.",
     "The deed to the dungeon itself.",
     "A map to a far greater treasure.",
     "Grateful prisoners who become allies.",
     "A bound spirit that offers one great favour.")
DTAB(twist,
     "The relic is a key, and something else has just woken up.",
     "The patron who hired the party is the boss's sibling.",
     "The villain was holding back something far worse in the vault.",
     "A rival party arrives to claim the prize.",
     "The treasure is cursed and marks its bearer for a powerful enemy.",
     "A survivor reveals this was only the first of three vaults.",
     "The boss's journal names a party member's relative.",
     "The dungeon starts to collapse. Get out in 10 rounds.",
     "The missing people are found, but they have changed.",
     "The way out leads somewhere else entirely.")
}  // namespace fantasy

#undef DTAB
#define DT(ns, k) Table{#k, ns::k, (int)(sizeof(ns::k) / sizeof(ns::k[0]))}
static const Table FANTASY[] = {DT(fantasy, adjective), DT(fantasy, place), DT(fantasy, hook), DT(fantasy, entrance),
    DT(fantasy, guardian), DT(fantasy, puzzle), DT(fantasy, trick), DT(fantasy, boss), DT(fantasy, terrain),
    DT(fantasy, reward), DT(fantasy, twist)};
#undef DT
constexpr int TABLE_COUNT = 11;
enum Key { ADJECTIVE, PLACE, HOOK, ENTRANCE, GUARDIAN, PUZZLE, TRICK, BOSS, TERRAIN, REWARD, TWIST };

}  // namespace dtab
