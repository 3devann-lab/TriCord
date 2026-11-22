#ifndef LOADING_VIEW_H
#define LOADING_VIEW_H

#include <string>

namespace UI {
namespace LoadingView {

// The connecting/loading screen shared by login and the server list. Animated from the clock, so
// callers only need to keep redrawing while it shows.
void drawTop();
void drawBottom();

} // namespace LoadingView
} // namespace UI

#endif // LOADING_VIEW_H
