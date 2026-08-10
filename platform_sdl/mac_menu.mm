#include "mac_menu.h"

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>

#include "sdl_audio.h"

// The menu items need an Objective-C object to send their action to, and it
// has to outlive the call that builds them, so it is a file-scope singleton
// rather than something owned by the menu.
@interface PhxAudioMenuTarget : NSObject {
 @public
  SdlAudio* audio;
  NSMenu* menu;
}
- (void)pick:(id)sender;
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

- (void)refreshTicks {
  if (!audio) return;
  int current = audio->current();
  for (NSMenuItem* item in menu.itemArray) {
    item.state = (item.tag == current) ? NSControlStateValueOn
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
    [g_target refreshTicks];
  }
}

#endif  // __APPLE__
