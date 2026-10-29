#pragma once
#include <Arduino.h>

// Line-art dice (coin, prism, tetrahedron, cube, octahedron, d10 kite,
// dodecahedron, icosahedron) with facet lines, drawn around (cx, cy) with
// radius r. Ported from the RPG Watch dice roller.
namespace dieshape {

// rot: radians in the screen plane; squash: 0..1 horizontal scale, faking a
// tumble around the vertical axis. Any number of sides works; unusual ones
// get the d20 shape.
void draw(int sides, float cx, float cy, float r, float rot, float squash,
          uint16_t fill, uint16_t edge, uint16_t facet, float edgeW = 3.0f);

}  // namespace dieshape
