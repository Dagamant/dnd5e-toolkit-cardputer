#pragma once
// Word lists for the Dungeon23 app's generators. Monsters, traps and loot come
// from the encounter generator and the Random Tables (SRD) instead.

namespace d23 {

// One a week, like the original challenge's weekly prompts (our own list).
const char* const THEMES[] = {
    "Ancient", "Sunken", "Factory", "Library", "Gold", "Fungal", "Frozen", "Burning", "Bone", "Clockwork",
    "Flooded", "Overgrown", "Haunted", "Royal", "Prison", "Temple", "Market", "Mine", "Garden", "Laboratory",
    "Barracks", "Tomb", "Sewer", "Observatory", "Forge", "Menagerie", "Mirror", "Salt", "Storm", "Glass",
    "Blood", "Silent", "Feast", "Dream", "Wax", "Iron", "Moon", "Plague", "Song", "Rot",
    "Crystal", "Puppet", "Ash", "Honey", "Shadow", "Coral", "Thorn", "Machine", "Star", "Ink",
    "Hollow", "Throne"};
constexpr int NTHEMES = sizeof(THEMES) / sizeof(THEMES[0]);

const char* const LIGHTS[] = {"Dark", "Dim", "Illuminated", "Outdoor", "Special"};
const char* const FEATURES[] = {"Monster", "Treasure", "Trap", "Special"};
const char* const KINDS[] = {"room", "puzzle", "setpiece", "faction", "boss"};
const char* const KIND_NAMES[] = {"Room", "Puzzle room", "Set piece", "Faction lair", "Boss"};

const char* const ROOM_ADJ[] = {
    "Flooded", "Collapsed", "Echoing", "Mossy", "Scorched", "Painted", "Narrow", "Vaulted", "Crooked", "Sealed",
    "Weeping", "Rusted", "Cold", "Smoky", "Gilded", "Shattered", "Hidden", "Upside-down", "Humming", "Rotten",
    "Webbed", "Silent", "Bloodstained", "Frosted", "Sunken", "Bright", "Dusty", "Tilted", "Singing", "Bare"};
const char* const ROOM_NOUN[] = {
    "Chapel", "Ossuary", "Kitchen", "Armory", "Cistern", "Gallery", "Barracks", "Shrine", "Study", "Larder",
    "Crypt", "Workshop", "Bathhouse", "Kennel", "Throne Room", "Well", "Vestibule", "Library", "Prison Cells",
    "Stair Hall", "Storeroom", "Guardroom", "Garden", "Forge", "Pool", "Dormitory", "Map Room", "Chimney",
    "Bridge", "Counting House", "Mushroom Farm", "Laboratory", "Feast Hall", "Observatory", "Tomb"};

const char* const SIGHTS[] = {
    "Rubble and a toppled pillar.", "Faded murals of a forgotten war.", "A dry fountain full of coins.",
    "Rows of empty iron hooks.", "Bones swept into a neat pile.", "Roots break through the ceiling.",
    "A ring of burnt-out candles.", "Water drips into a black pool.", "Broken furniture, burnt for warmth.",
    "Chalk marks counting days on the wall.", "A statue with its face chiselled off.", "Thick webs across every corner.",
    "A rusted cage hangs from a chain.", "Moss glows faintly green.", "Old bedrolls and a cold firepit.",
    "Shelves of cracked clay jars.", "A mosaic floor of a staring eye.", "Scratches lead to a blank wall.",
    "A broken altar under a hole in the roof.", "Barrels of something that smells like vinegar."};
const char* const SMELLS[] = {"damp stone", "smoke", "rot", "incense", "wet fur", "copper", "mildew",
                              "sulphur", "old blood", "cold ash", "flowers, oddly", "oil"};
const char* const SOUNDS[] = {"dripping water", "distant chanting", "scratching in the walls", "wind through cracks",
                              "a slow heartbeat", "chains moving", "nothing at all", "faint laughter",
                              "grinding stone", "buzzing flies"};

const char* const SPECIALS[] = {
    "A talking skull that trades secrets for gossip.", "A pool: drinking heals 1d8, but only once a day.",
    "A statue whose eyes follow the brightest light.", "An altar: an offering of gold grants advantage on the next save.",
    "A mirror that shows the room as it was 100 years ago.", "A lever with no obvious purpose (it floods the level below).",
    "A sleeping ghost who wakes if anyone lies.", "A fountain of wine. Anyone who drinks is charmed by the next host.",
    "Gravity is sideways here.", "A door painted on the wall opens for anyone who knocks three times.",
    "A shrine to a forgotten god: pray for a cryptic vision.", "Whispers name one of the party.",
    "A magic circle: spells cast inside it are maximised (and attract attention).",
    "A brass head that answers one question a day, truthfully but unhelpfully."};
const char* const SECRETS[] = {
    "Secret door behind the tapestry.", "A loose flagstone hides a pouch of gems.", "A false bottom in the chest.",
    "The statue's base slides away to a crawlspace.", "A hidden lever opens a shortcut to the entrance.",
    "Writing under the dust gives a password.", "A spyhole looks into the next room.",
    "A sealed letter from a missing adventurer.", "The fireplace turns to reveal a stair."};

struct Puzzle { const char* puzzle; const char* solution; const char* failure; };
const Puzzle PUZZLES[] = {
    {"Five levers; only the right order opens the door. Faded numbers on the wall.",
     "Count the skulls carved in each alcove; pull the levers fewest to most. Or force it (DC 18 Strength).",
     "Darts from the walls: DC 13 Dex save, 2d6 piercing, and the levers reset."},
    {"A riddling door: 'I have keys but open no locks.'", "Answer 'a piano' - or play the carved keys on the frame.",
     "The door bites: 1d10 piercing to whoever touched it."},
    {"Scales that must balance a golden idol.", "Equal weight of anything: sand, coins, a party member's armour.",
     "The floor tilts; everything slides into the pit (DC 14 Dex save)."},
    {"Four statues pointing in different directions.", "Turn them all toward the door they fear (the one with a sun).",
     "The statues animate for 1 round and shove everyone back to the start."},
    {"A floor of tiles marked with runes; some sink.", "Step only on runes that spell the god's name (found in the chapel).",
     "Sinking tiles drop the walker 10 ft into icy water."},
    {"A locked chest that speaks only in questions.", "Answer every question with a question.",
     "It locks forever (until a knock spell) and screams for the guards."},
    {"Water rises when the door closes.", "Plug the inflow with the statue's head, or find the drain under the moss.",
     "The room fills in 5 rounds: swim or drown."}};

const char* const BEHAVIOR[] = {
    "Toys with intruders before striking.", "Fights only to guard its hoard; never pursues.",
    "Bargains first, attacks if insulted.", "Hunts from the shadows, hit and run.", "Rages when hurt; fights to the death.",
    "Commands minions and stays out of reach.", "Mournful and old; wants to be remembered.",
    "Fanatical: obeys a master elsewhere in the dungeon."};
const char* const WANTS[] = {"its stolen egg back", "news of the outside world", "a worthy opponent", "to be freed from a curse",
                             "more gold", "the head of a rival on another level", "to sleep for another century",
                             "a name that it has forgotten"};
const char* const FEARS[] = {"fire", "holy symbols", "its own reflection", "a particular song", "the thing on the level below",
                             "running water", "being forgotten", "silver"};

const char* const FACTION_ADJ[] = {"Pale", "Iron", "Hollow", "Ashen", "Grinning", "Drowned", "Gilded", "Red", "Silent", "Hungry"};
const char* const FACTION_NOUN[] = {"Brotherhood", "Court", "Tribe", "Choir", "Company", "Circle", "Clan", "Host", "Order", "Band"};
const char* const DESIRES[] = {"control of the stairs down", "food, desperately", "a holy relic held by rivals",
                               "to escape the dungeon", "to awaken their sleeping god", "slaves for the mines",
                               "revenge on the last adventurers", "trade with the surface"};
const char* const DEFENSES[] = {"Barricades and murder holes", "Trained giant rats as alarms", "A collapsing bridge",
                                "Archers in the rafters", "Poisoned spikes in the approach", "Hostages kept at the door",
                                "A pet ogre on a chain", "Smoke and a hidden escape tunnel"};

// Omens of a wandering monster; %s is what is coming.
const char* const OMENS[] = {"Fresh tracks: %s.", "The distant noise of %s.", "A discarded meal - %s ate here.",
                             "A torn scrap of cloth and the smell of %s.", "A warning scratched on the wall: %s!",
                             "Bones gnawed by %s.", "A dropped weapon. Nearby: %s.", "Eerie silence, then the sound of %s."};

const char* const ACTIVITY[] = {"Asleep, mostly", "On guard, alert", "Eating", "Arguing among themselves", "Hiding in ambush",
                                "Tending something", "Hurt from a recent fight", "Lost and nervous", "Performing a rite",
                                "Counting loot"};

const char* const LEVEL_NOUN[] = {"Halls", "Vaults", "Warrens", "Crypts", "Galleries", "Cisterns", "Forges", "Stacks",
                                  "Pits", "Barracks", "Gardens", "Catacombs", "Caverns", "Cells", "Chambers", "Depths"};
const char* const DUNGEON_NOUN[] = {"Spire", "Deep", "Underhall", "Pit", "Labyrinth", "Vault", "Undercroft", "Barrow",
                                    "Citadel", "Warren", "Maw", "Stair"};
const char* const DUNGEON_ADJ[] = {"Sunless", "Endless", "Shattered", "Drowned", "Hungry", "Whispering", "Forgotten",
                                   "Weeping", "Ashen", "Crooked", "Bottomless", "Iron"};
const char* const HISTORY[] = {
    "A wizard's tower that sank into the earth overnight.", "A dwarven mine that dug too deep.",
    "The prison of a god, built by the god's own priests.", "A royal tomb, expanded by every heir.",
    "A city that fled underground from a plague.", "A temple to a forgotten god, now home to anything that crawls in.",
    "The lair of a dragon, shaped by centuries of its servants.", "A bandit hideout built on top of something much older."};
const char* const HOOKS[] = {
    "A merchant pays 100 gp for the return of a stolen ledger, last seen on level %d.",
    "A child went missing near the entrance; a toy was found on the stairs.",
    "A scholar wants rubbings of the murals on level %d.", "The baron's heir went in a week ago with six guards.",
    "Strange lights rise from the entrance on moonless nights.", "A map sold in the tavern shows a vault on level %d."};

}  // namespace d23
