#ifndef LOADING_VIEW_H
#define LOADING_VIEW_H

#include <string>

namespace UI {
namespace LoadingView {

// The connecting/loading screen shared by login and the server list. Animated from the clock, so
// callers only need to keep redrawing while it shows.
void drawTop();
void drawBottom();
// Call every frame the loading screen shows. Holding L+R, or no connection after 20 seconds, switches
// lookups of Discord's hosts to Google DNS for networks that block them.
void update();

} // namespace LoadingView
} // namespace UI

#endif // LOADING_VIEW_H
