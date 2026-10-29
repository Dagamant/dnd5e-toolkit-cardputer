#pragma once

// Word lists for the Hex Map app's keyed hexes (original text).
namespace hexmap {

const char* const MAP_ADJ[] = {"Broken", "Green", "Howling", "Golden", "Ashen", "Misty", "Iron", "Silver",
                               "Wild", "Red", "Thorn", "Hollow", "Sunken", "Bright", "Grey", "Lonely"};
const char* const MAP_NOUN[] = {"Marches", "Reach", "Vale", "Wilds", "Downs", "Fells", "Barrens", "Weald",
                                "Moors", "Expanse", "Borderlands", "Highlands", "Lowlands", "Heath"};

// Settlement names: a first part and an ending that suits where it stands.
const char* const TOWN_START[] = {"Ash", "Black", "Bram", "Cold", "Corn", "Elder", "Fair", "Fen", "Gold", "Green",
                                  "Hart", "High", "Kings", "Long", "Mill", "Oak", "Red", "Rook", "Salt", "Stone",
                                  "Thorn", "West", "Wolf", "Wyn"};
const char* const END_RIVER[] = {"ford", "bridge", "mouth", "wade"};
const char* const END_COAST[] = {"haven", "port", "sands", "cove"};
const char* const END_HILL[] = {"hill", "barrow", "crag", "top"};
const char* const END_FOREST[] = {"wood", "holt", "glade", "shaw"};
const char* const END_PLAIN[] = {"ton", "wick", "field", "stead", "ham", "by"};

const char* const VILLAGE_TRADE[] = {"sheep and wool", "barley and beer", "a stone quarry", "charcoal burning",
                                     "eels and reeds", "apple orchards", "goat cheese", "salt pans", "a silver seam",
                                     "horse breeding", "pottery clay", "honey and wax"};
const char* const VILLAGE_TROUBLE[] = {"Livestock vanish at night.", "The well has gone foul.",
                                       "Two families are feuding over a boundary stone.", "Bandits demand a toll on the road.",
                                       "A child came back from the woods speaking in riddles.", "The harvest is failing.",
                                       "The headman is sick and his heir is a fool.", "Strange lights hover over the graveyard.",
                                       "Tax collectors are due and the coffers are empty.", "A beast's tracks circle the palisade.",
                                       "Nothing - it's peaceful. Suspiciously so.", "Pilgrims crowd the shrine and tempers fray."};
const char* const TOWN_EXTRA[] = {"A walled market town with a weekly fair.", "A river port with warehouses and a customs house.",
                                  "A temple town grown around an old shrine.", "A garrison town under a stone keep.",
                                  "A mining town, rich and rough.", "A crossroads town full of caravans."};

const char* const RUIN_KIND[] = {"a toppled watchtower", "a burned manor house", "a roofless chapel", "a ring of broken columns",
                                 "a collapsed bridge fort", "an overgrown villa", "a sunken temple", "a ruined mill",
                                 "a shattered statue garden", "a half-buried city gate"};
const char* const RUIN_STATE[] = {"choked with ivy", "blackened by an old fire", "split by tree roots", "half flooded",
                                  "picked clean by looters", "eerily intact inside", "humming with faint magic",
                                  "marked with fresh graffiti"};

const char* const DUNGEON_WAY[] = {"A stair hidden under a fallen menhir", "A cave mouth breathing cold air",
                                   "A sinkhole with a rope still tied at the top", "A sealed tomb door with a broken seal",
                                   "A mine shaft with rotten timbers", "A well that drops into a cavern",
                                   "A crack in a cliff face, shoulder wide", "A trapdoor in a ruined cellar"};

const char* const LAIR_SIGN[] = {"Gnawed bones litter the approach.", "Trees are scored with deep claw marks.",
                                 "A foul musk hangs in the air.", "Crude totems warn travellers off.",
                                 "Nothing grows within a stone's throw.", "Tracks lead in, none lead out."};

const char* const CAMP_KIND[] = {"a bandit camp", "a hunting camp", "a raiders' war camp", "a smugglers' cache",
                                 "a deserters' hideout", "a pilgrim encampment", "a cultists' circle", "an outlaw lodge"};

const char* const TOWER_KIND[] = {"a crooked wizard's tower", "an old signal tower", "a hermit's stone tower",
                                  "an astronomer's observatory", "a necromancer's spire", "a crumbling lighthouse inland"};

const char* const SHRINE_TO[] = {"a forgotten harvest goddess", "the god of travellers", "a storm god", "a local saint",
                                 "the moon", "the lord of the dead", "a river spirit", "a war god", "a god of secrets"};
const char* const SHRINE_GIFT[] = {"Praying here grants Heroic Inspiration once.", "Its spring heals 1d8 HP once a day.",
                                   "Offerings left here vanish overnight.", "An old priest keeps it and knows local lore.",
                                   "It has been desecrated - something is angry.", "Pilgrims leave coins in the bowl (2d10 gp)."};

const char* const LANDMARK[] = {"A circle of standing stones", "The skull of an enormous dragon", "A lone burned oak",
                                "A giant's grave mound", "A statue of a forgotten king, face chiselled off",
                                "A natural stone arch", "A gibbet at a crossroads", "A tree hung with hundreds of bells",
                                "A crater of black glass", "A waterfall that flows upward at dusk",
                                "An abandoned siege engine", "A cairn of carved skulls"};

const char* const WONDER[] = {"Hot springs that ease aches (short rest counts as long for HD)", "A fairy ring of red mushrooms",
                              "A cave of glowing crystals", "A lake so clear you can see a sunken village",
                              "A grove where it is always spring", "Floating stones drifting above a meadow",
                              "A tar pit with bones of strange beasts", "A field of whispering grass that repeats words",
                              "A meteor, still warm", "A tree with silver leaves"};

const char* const RESOURCE[] = {"Rare healing herbs (enough for 2 Potions of Healing with an herbalism kit)",
                                "A seam of iron ore", "Wild game - foraging DC drops by 5 here", "A grove of yew for bows",
                                "Mushrooms: delicious or deadly (DC 13 Nature)", "A vein of silver",
                                "Clay and good stone for building", "Beehives in the cliffs", "A clear spring",
                                "Shards of an old meteorite (worth 50 gp)"};

const char* const WATER_FEATURE[] = {"A shipwreck on a reef", "A small wooded island", "A fishing village on stilts",
                                     "A sea stack with a nest on top", "A whirlpool", "A sunken tower showing at low tide"};

}  // namespace hexmap
