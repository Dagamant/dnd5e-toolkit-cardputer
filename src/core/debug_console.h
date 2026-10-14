#pragma once
// Serial debug console (115200 baud), one command per line:
//   key <name>   inject a key: up down left right enter back del tab space,
//                a single character, or fn+<char> / shift+<char>
//   type <text>  inject each character of text
//   shot         dump the frame buffer: "SHOT 240 135" then RGB565 rows as hex
//   mem          print free heap
namespace debug_console {
void update();
}
