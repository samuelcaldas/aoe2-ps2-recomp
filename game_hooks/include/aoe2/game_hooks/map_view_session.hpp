#pragma once

class PS2Runtime;

namespace aoe2::game_hooks {
// Runtime must outlive the session. Construct and close only at guest safe points;
// Close requires guest execution stopped and must precede runtime shutdown/_Exit.
class MapViewSession {
public:
    explicit MapViewSession(PS2Runtime& runtime);
    ~MapViewSession();
    MapViewSession(const MapViewSession&) = delete;
    MapViewSession& operator=(const MapViewSession&) = delete;
    MapViewSession(MapViewSession&&) = delete;
    MapViewSession& operator=(MapViewSession&&) = delete;
    void Close();
private:
    PS2Runtime* runtime_ = nullptr;
};
}
