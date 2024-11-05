#pragma once

#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <queue>
#include <vector>
#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "../ServerClient/Client.h"
#include "../HobbitGameManager/HobbitGameManager.h"
#include "../HobbitGameManager/NPC.h"

#include "Utility.h"
#include "MainPlayer.h"
#include "ConnectedPlayer.h"

// Main client class
class HobbitClient {
public:
    HobbitClient(std::string initialServerIp = "")
        : serverIp(std::move(initialServerIp)), running(false), processMessages(false) {
    }
    ~HobbitClient() { stop(); }

    int start();
    int start(const std::string& ip);
    void stop();
private:
    Client client;
    HobbitGameManager hobbitGameManager;

    std::vector<uint64_t> guids;

    std::string serverIp;


    std::thread updateThread;
    std::atomic<bool> running;
    std::atomic<bool> processMessages = false;


    static constexpr int MAX_PLAYERS = 7;
    ConnectedPlayer connectedPlayers[MAX_PLAYERS];

    MainPlayer mainPlayer;
    
    void update();
    void readMessage();
    void readGameMessage(int senderID, std::queue<uint8_t>& gameData);
    void writeMessage();
    
    void onEnterNewLevel();
    void onExitLevel() { processMessages = false; }

    void onOpenGame();
    void onCloseGame() { processMessages = false; }

    void onClientListUpdate(const std::queue<uint8_t>&);

    std::vector<uint64_t> getPlayersNpcGuid();
};

int HobbitClient::start() {
    std::cout << "Enter server IP address: ";
    std::cin >> serverIp;
    std::cin.ignore();

    return start(serverIp);
}
int HobbitClient::start(const std::string& ip) {
    serverIp = ip;

    client.addListener([this](const std::queue<uint8_t>& clientIDs) {
        onClientListUpdate(clientIDs);
        });

    if (client.start(serverIp)) return 1;
    

    while (!hobbitGameManager.isGameRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "You must open The Hobbit 2003 game!" << std::endl;
    }

    running = true;
    hobbitGameManager.addListenerEnterNewLevel([this] { onEnterNewLevel(); });
    hobbitGameManager.addListenerExitLevel([this] { onExitLevel(); });
    hobbitGameManager.addListenerOpenGame([this] { onOpenGame(); });
    hobbitGameManager.addListenerCloseGame([this] { onCloseGame(); });

    hobbitGameManager.start();

    onOpenGame();
    updateThread = std::thread(&HobbitClient::update, this);
    return 0;
    
}

void HobbitClient::stop() {
    running = false;
    client.stop();

    if (updateThread.joinable()) {
        updateThread.join();
    }
}
void HobbitClient::update() {
    while (running ){
        if (!hobbitGameManager.isOnLevel() || !processMessages)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        readMessage();
        for (int i = 0; i < MAX_PLAYERS; ++i)
        {
            connectedPlayers[i].processPlayer(client.getClientID());
        }
        writeMessage();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void HobbitClient::readMessage() {

    BaseMessage textMessageOpt = client.frontTextMessage();
    if (textMessageOpt.message.size() > 0) {
        std::cout << "Received Text Message from " << int(textMessageOpt.senderID) << ": ";
        // Assuming textMessageOpt.message is a queue of characters or strings
        std::string fullMessage;
        while (!textMessageOpt.message.empty()) {
            fullMessage += textMessageOpt.message.front(); // Get the front message
            textMessageOpt.message.pop(); // Remove the front message from the queue
        }

        std::cout << fullMessage << std::endl;

        client.popFrontTextMessage();
    }

    BaseMessage eventMessageOpt = client.frontEventMessage();
    if (eventMessageOpt.message.size() > 0) {
        readGameMessage(eventMessageOpt.senderID, eventMessageOpt.message);
        client.popFrontEventMessage();
    }

    std::map<uint8_t, BaseMessage> snapshotMessages = client.snapMessage();
    for (auto& pair : snapshotMessages) {
        readGameMessage(pair.first, pair.second.message);
        client.clearSnapMessage();
    }

}
void HobbitClient::writeMessage() {
    BaseMessage snap(SNAPSHOT_MESSAGE, client.getClientID());
    snap.message = mainPlayer.write();
    if (snap.message.size()>0) client.sendMessage(snap);
}
void HobbitClient::readGameMessage(int senderID, std::queue<uint8_t>& gameData) {
    std::cout << "Received Message from client " << senderID << std::endl;

    while (!gameData.empty()) {
        DataLabel label = static_cast<DataLabel>(gameData.front());
        gameData.pop();

        uint8_t size_tmp = gameData.front();
        gameData.pop();

        if (label == DataLabel::CONNECTED_PLAYER_SNAP)
        {
            auto it = std::find_if(std::begin(connectedPlayers), std::end(connectedPlayers),
                [&](const ConnectedPlayer& p) { return p.id == senderID; });
            if (it != std::end(connectedPlayers)) {
                it->readConectedPlayerSnap(gameData);
            }
            else {
                std::cerr << "ERROR: Unregistered player id: " << senderID << std::endl;
                connectedPlayers[0].readConectedPlayerSnap(gameData);
            }

        }
        else if (label == DataLabel::CONNECTED_PLAYER_LEVEL)
        {
            auto it = std::find_if(std::begin(connectedPlayers), std::end(connectedPlayers),
                [&](const ConnectedPlayer& p) { return p.id == senderID; });
            if (it != std::end(connectedPlayers)) {
                it->readConectedPlayerSnap(gameData);
            }
            else {
                std::cerr << "ERROR: Unregistered player id: " << senderID << std::endl;
                connectedPlayers[0].readConectedPlayerSnap(gameData);
            }
        }
        else
        {
            std::cerr << "ERROR: Unknown label received" << std::endl;
        }
    }
}


void HobbitClient::onEnterNewLevel() {

    if (guids.size() == 0)
        guids = getPlayersNpcGuid();

    mainPlayer.setHobbitProcessAnalyzer(hobbitGameManager.getHobbitProcessAnalyzer());

    for (ConnectedPlayer connectedPlayer : connectedPlayers)
        connectedPlayer.setHobbitProcessAnalyzer(hobbitGameManager.getHobbitProcessAnalyzer());

    mainPlayer.readPtrs();
    for (int i = 0; i < MAX_PLAYERS; ++i)
    {
        connectedPlayers[i].npc.setNCP(guids[i], &hobbitGameManager.getHobbitProcessAnalyzer());
    }
    processMessages = true;
}
void HobbitClient::onOpenGame()
{
    processMessages = false;
    mainPlayer.setHobbitProcessAnalyzer(hobbitGameManager.getHobbitProcessAnalyzer());

    for(ConnectedPlayer connectedPlayer : connectedPlayers)
        connectedPlayer.setHobbitProcessAnalyzer(hobbitGameManager.getHobbitProcessAnalyzer());
    
    if (hobbitGameManager.isOnLevel())
        onEnterNewLevel();
}


void HobbitClient::onClientListUpdate(const std::queue<uint8_t>&) {
    std::queue<uint8_t> connectedClients = client.getConnectedClients();
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        connectedPlayers[i].id = connectedClients.empty() ? -1 : connectedClients.front();
        if (!connectedClients.empty()) connectedClients.pop(); // Remove the front element from the queue
    }
}
std::vector<uint64_t> HobbitClient::getPlayersNpcGuid() {
    std::ifstream file;
    std::string filePath = "FAKE_BILBO_GUID.txt";

    // Open the file, prompting the user if it doesn't exist
    while (!file.is_open()) {
        file.open(filePath);
        if (!file.is_open()) {
            std::cout << "File not found. Enter the path to FAKE_BILBO_GUID.txt or 'q' to quit: ";
            std::string input;
            std::getline(std::cin, input);
            if (input == "q") return {}; // Quit if user enters 'q'
            filePath = input;
        }
    }

    std::vector<uint64_t> tempGUID;
    std::string line;

    // Read each line from the file
    while (std::getline(file, line)) {
        // Find the position of the underscore
        size_t underscorePos = line.find('_');
        if (underscorePos != std::string::npos) {
            // Split the line into two parts
            std::string part2 = line.substr(0, underscorePos); // Before the underscore
            std::string part1 = line.substr(underscorePos + 1); // After the underscore

            // Concatenate the parts in reverse order
            std::string combined = part2 + part1;

            // Convert the combined string to uint64_t
            uint64_t guid = std::stoull(combined, nullptr, 16); // Convert from hex string to uint64_t
            tempGUID.push_back(guid);
        }
    }

    std::cout << "FOUND FILE!" << std::endl;
    return tempGUID;
}