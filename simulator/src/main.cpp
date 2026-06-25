#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include "AvatarService.h"
#include "BuiltinTools.h"
#include "EventBus.h"
#include "SimApiServer.h"
#include "SimHardware.h"
#include "ToolRegistry.h"

namespace {
volatile std::sig_atomic_t gRunning = 1;

void onSignal(int) {
    gRunning = 0;
}

void ensureLayout(stackchu::IStorage& storage) {
    storage.mkdir("/stackyan");
    storage.mkdir("/stackyan/tools");
    storage.mkdir("/stackyan/workflows");
    storage.mkdir("/stackyan/logs");
    storage.mkdir("/stackyan/memory");
    if (!storage.exists("/stackyan/config.json")) {
        storage.writeText("/stackyan/config.json", "{\n  \"device_name\": \"stackyan-sim\",\n  \"version\": 1\n}\n");
    }
}
}

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::cout << "StackYan mac_simulator booting..." << std::endl;

    stackyan::sim::SimHardware hardware;
    if (!hardware.begin()) {
        std::cerr << "SimHardware begin failed" << std::endl;
        return 1;
    }
    ensureLayout(hardware.storage());

    stackyan::EventBus events;
    events.publish("system.boot", "mac_simulator");

    stackyan::ToolRegistry registry;
    stackyan::AvatarService avatar;
    stackyan::registerBuiltinTools(registry, hardware, avatar, events);

    stackyan::sim::SimApiServer server(hardware, registry, events);
    if (!server.begin(8080)) return 1;

    std::cout << "Open http://localhost:8080/" << std::endl;
    while (gRunning) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "StackYan mac_simulator stopped." << std::endl;
    return 0;
}
