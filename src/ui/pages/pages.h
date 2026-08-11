#pragma once

#include <memory>

#include "../ui_core.h"

class PhoenixModel;

// Page order is the signal order: sources, then what they drive, then output.
std::unique_ptr<IPage> makeHomePage(PhoenixModel& m);
std::unique_ptr<IPage> makeChaosPage(PhoenixModel& m);
std::unique_ptr<IPage> makeOscPage(PhoenixModel& m);
std::unique_ptr<IPage> makeSeqPage(PhoenixModel& m);
std::unique_ptr<IPage> makeLogicPage(PhoenixModel& m);
std::unique_ptr<IPage> makeFilterPage(PhoenixModel& m);
std::unique_ptr<IPage> makeDrumPage(PhoenixModel& m);
std::unique_ptr<IPage> makeSpacePage(PhoenixModel& m);
std::unique_ptr<IPage> makeMixPage(PhoenixModel& m);
std::unique_ptr<IPage> makeConfigPage(PhoenixModel& m);
std::unique_ptr<IPage> makeProjectPage(PhoenixModel& m);
std::unique_ptr<IPage> makeHelpPage(PhoenixModel& m);
