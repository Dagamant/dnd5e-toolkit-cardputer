#pragma once
// Little procedural sound effects through the Cardputer speaker.
namespace sfx {
void begin();
void update();     // advances queued multi-note effects; call every frame
void click();      // menu move
void select();     // confirm
void back();
void error();
void diceRattle(); // start tumbling clatter
void crit();       // natural max: rising arpeggio
void fumble();     // natural 1: descending groan
void land();       // dice settle
void alarm();      // timer / turn alert
}  // namespace sfx
