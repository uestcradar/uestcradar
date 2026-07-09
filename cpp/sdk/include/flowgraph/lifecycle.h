#ifndef CYCORE_LIFECYCLE_H
#define CYCORE_LIFECYCLE_H

namespace cy::flowgraph::lifecycle {

enum class State : char {
    IDLE,
    INITIALISED,
    RUNNING,
    REQUESTED_PAUSE,
    PAUSED,
    REQUESTED_STOP,
    STOPPED,
    ERROR
};

} // namespace cy::flowgraph::lifecycle

#endif // CYCORE_LIFECYCLE_H
