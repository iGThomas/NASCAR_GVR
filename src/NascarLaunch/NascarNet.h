// NascarNet - linked races ([Multiplayer] in nascar_settings.ini).
//
// The race has its own UDP netcode: the host runs "-mode multi -server", each other player
// "-mode multi -client <host IPv4>", always to UDP port 22544. NascarNet adds the internet part:
// with a relay server, every launcher talks only OUTWARD to the relay (no router setup), and a
// small agent inside the launcher passes the race's packets through it:
//   join PC:  race -client 127.0.0.1  ->  agent on 127.0.0.1:22544  ->  relay
//   host PC:  relay  ->  agent, one local socket per player (127.0.0.<n+1>)  ->  race on :22544
// Without a relay (same network), the launcher just starts the race with the host's address.
#pragma once
#include <string>

struct NetSettings {
    std::string mode;          // "host" or "join"
    std::string relay;         // "name:port" (port optional); empty = direct, same network
    std::string room;          // room name the host picks and the others type
    std::string hostAddress;   // direct join only: the host's IPv4 address
    int players;               // host: players including the host, 2-10
    std::string track, series; // host: the race everyone drives
    int laps;
};

// Builds the race's arguments after "-mode multi ..." (track, series if installed, own car, laps,
// display switches, ExtraArgs).
typedef std::string (*NetRaceArgsFn)(const std::string& track, const std::string& series, int laps);
// Starts the race and waits for it to end; returns its exit code.
typedef unsigned long (*NetLaunchFn)(const std::string& args);
typedef void (*NetLogFn)(const char* fmt, ...);

// Runs a linked race (lobby window, race, relay agent). Returns the launcher's exit code.
int net_run(const NetSettings& ns, NetRaceArgsFn raceArgs, NetLaunchFn launch, NetLogFn log);
