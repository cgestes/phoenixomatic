#include "mac_menu.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include "sdl_audio.h"
#include "../src/dsp/audio_config.h"
#include "../src/core/model.h"

// The menu items need an Objective-C object to send their action to, and it
// has to outlive the call that builds them, so it is a file-scope singleton
// rather than something owned by the menu.
// Set before the menu is built, so the transport item knows what to start.
static PhoenixModel* g_model = nullptr;

void installTransportMenu(PhoenixModel* model) { g_model = model; }

@interface PhxAudioMenuTarget : NSObject {
 @public
  SdlAudio* audio;
  NSMenu* menu;
  NSMenu* rates;
}
- (void)pick:(id)sender;
- (void)toggleTransport:(id)sender;
- (void)pickRate:(id)sender;
- (void)refreshTicks;
@end

@implementation PhxAudioMenuTarget

- (void)pick:(id)sender {
  NSMenuItem* item = (NSMenuItem*)sender;
  // Tag is the device index; -1 is the system default.
  int index = static_cast<int>(item.tag);
  if (audio) audio->select(index);
  [self refreshTicks];
}

// The tag is the rate in hertz, which is also what the label says, so there is
// nothing to keep in step between them.
// Transport, for the times your hands are on the mouse. The keyboard has G.
- (void)toggleTransport:(id)sender {
  (void)sender;
  if (g_model) g_model->togglePlay();
}

- (void)pickRate:(id)sender {
  NSMenuItem* item = (NSMenuItem*)sender;
  if (audio) audio->setRate(static_cast<int>(item.tag));
  [self refreshTicks];
}

- (void)refreshTicks {
  if (!audio) return;
  int current = audio->current();
  for (NSMenuItem* item in menu.itemArray) {
    item.state = (item.tag == current) ? NSControlStateValueOn
                                       : NSControlStateValueOff;
  }
  int hz = audio->rate();
  for (NSMenuItem* item in rates.itemArray) {
    item.state = (item.tag == hz) ? NSControlStateValueOn
                                  : NSControlStateValueOff;
  }
}

@end

static PhxAudioMenuTarget* g_target = nil;

void installAudioMenu(SdlAudio* audio) {
  if (!audio) return;
  @autoreleasepool {
    NSMenu* main = [NSApp mainMenu];
    // SDL builds a minimal menu bar on macOS; if it has not yet, there is
    // nothing to hang this off and the app still works without it.
    if (!main) return;

    NSMenu* submenu = [[NSMenu alloc] initWithTitle:@"Audio Output"];
    g_target = [[PhxAudioMenuTarget alloc] init];
    g_target->audio = audio;
    g_target->menu = submenu;

    NSMenuItem* def = [[NSMenuItem alloc] initWithTitle:@"System Default"
                                                action:@selector(pick:)
                                         keyEquivalent:@""];
    def.target = g_target;
    def.tag = -1;
    [submenu addItem:def];
    [submenu addItem:[NSMenuItem separatorItem]];

    for (int i = 0; i < audio->count(); ++i) {
      const char* name = audio->name(i);
      NSMenuItem* item =
          [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:name]
                                     action:@selector(pick:)
                              keyEquivalent:@""];
      item.target = g_target;
      item.tag = i;
      [submenu addItem:item];
    }

    NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:@"Audio Output"
                                                 action:nil
                                          keyEquivalent:@""];
    [top setSubmenu:submenu];
    [main addItem:top];

    // The rate sits beside the device because it is the same kind of choice: a
    // property of this machine rather than of the instrument. The low ones are
    // there on purpose -- at 8 kHz the comparator aliases into something the
    // machine cannot make any other way.
    NSMenu* rate_menu = [[NSMenu alloc] initWithTitle:@"Sample Rate"];
    g_target->rates = rate_menu;
    for (int i = 0; i < kRateCount; ++i) {
      NSString* title = [NSString stringWithFormat:@"%d Hz", kRates[i]];
      NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                   action:@selector(pickRate:)
                                            keyEquivalent:@""];
      item.target = g_target;
      item.tag = kRates[i];
      [rate_menu addItem:item];
    }
    NSMenuItem* rate_top = [[NSMenuItem alloc] initWithTitle:@"Sample Rate"
                                                     action:nil
                                              keyEquivalent:@""];
    [rate_top setSubmenu:rate_menu];
    [main addItem:rate_top];

    // Play/stop where a mouse can reach it, alongside the two device menus.
    if (g_model) {
      NSMenu* tm = [[NSMenu alloc] initWithTitle:@"Transport"];
      NSMenuItem* go = [[NSMenuItem alloc] initWithTitle:@"Go / Stop"
                                                  action:@selector(toggleTransport:)
                                           keyEquivalent:@"g"];
      go.target = g_target;
      [tm addItem:go];
      NSMenuItem* top2 = [[NSMenuItem alloc] initWithTitle:@"Transport"
                                                    action:nil
                                             keyEquivalent:@""];
      [top2 setSubmenu:tm];
      [main addItem:top2];
    }

    [g_target refreshTicks];
  }
}

#endif  // __APPLE__
