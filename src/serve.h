// Diorama - a small web server for Build And Run: serves a built game's folder on http://localhost:<port>/, so the
// browser allows the webcam (it does on localhost) and the game can be tried on this PC.
#pragma once
#include <string>
#include <vector>

namespace Serve {
bool start(const std::string& folder, int port);   // (again: just changes the folder)
void stop();
int port();
// what Play's page reported (log() and script errors), taken by the editor's Console
struct Log { std::string level, file, text; int line = 0; };
std::vector<Log> takeLogs();
}
