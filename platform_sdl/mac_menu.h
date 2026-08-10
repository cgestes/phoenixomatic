#pragma once

class SdlAudio;

// Adds an "Audio Output" menu to the macOS menu bar, listing the devices SDL
// found and ticking the one in use.
//
// Everywhere else this is an inline no-op *in the header*, not a stub in the
// .mm: only the macOS build compiles that file, so a definition living there
// leaves the web build with an undefined symbol.
#ifdef __APPLE__
void installAudioMenu(SdlAudio* audio);
#else
inline void installAudioMenu(SdlAudio*) {}
#endif
