#pragma once

class SdlAudio;

// Adds an "Audio Output" menu to the macOS menu bar, listing the devices SDL
// found and ticking the one in use.
//
// Everywhere else this is an inline no-op *in the header*, not a stub in the
// .mm: only the macOS build compiles that file, so a definition living there
// leaves the web build with an undefined symbol.
#ifdef __APPLE__
class PhoenixModel;

void installAudioMenu(SdlAudio* audio);
// Hands the menu the model so it can start and stop the machine. Call it
// before installAudioMenu; without it the Transport menu is simply absent.
void installTransportMenu(PhoenixModel* model);
#else
inline void installAudioMenu(SdlAudio*) {}
inline void installTransportMenu(PhoenixModel*) {}
#endif
