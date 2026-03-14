#pragma once
/**
 * Tournament.h
 * ============
 * Network client for Chess Tournament system.
 *
 * This is a drop-in network manager for students to connect their chess bots
 * to the tournament relay server.
 *
 * Usage:
 *   Chess* game = new Chess();
 *   TournamentClient client(game, "MyBot");
 *   client.connect("13.223.80.180", 5000);
 *
 *   // In your render loop:
 *   client.update();
 */

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <iostream>
#include <sstream>
#include <cstring>
#include <map>
#include <algorithm>

// Platform-specific socket includes
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET SocketType;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
    #define CLOSE_SOCKET(s) closesocket(s)
    #define WOULD_BLOCK_ERROR (WSAGetLastError() == WSAEWOULDBLOCK)
#else
    // macOS / Linux
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <netdb.h>
    typedef int SocketType;
    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_ERROR_VALUE -1
    #define CLOSE_SOCKET(s) close(s)
    #define WOULD_BLOCK_ERROR (errno == EWOULDBLOCK || errno == EAGAIN)
#endif

// Forward declaration
class Chess;

/**
 * TournamentClient
 * ================
 * Manages network communication for a chess bot in tournament mode.
 *
 * Protocol:
 *   - Messages are pipe-delimited: TARGET|PAYLOAD
 *   - FEN positions arrive as: ADMIN|FEN:<fen_string>
 *   - Moves are sent as: ADMIN|MOVE:srcIndex,dstIndex
 */
class TournamentClient {
public:
    // Connection states
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Error
    };

    // Message callback type
    using MessageCallback = std::function<void(const std::string& sender, const std::string& payload)>;

    Chess* _game;
private:
    std::string _botName;
    SocketType _socket;
    State _state;
    std::string _receiveBuffer;
    std::string _lastError;
    bool _waitingForAI;
    bool _moveReady;
    MessageCallback _messageCallback;

    // Logging
    std::vector<std::string> _log;
    static constexpr size_t MAX_LOG_ENTRIES = 100;

#ifdef _WIN32
    bool _wsaInitialized;
#endif

public:
    /**
     * Constructor
     * @param game Pointer to the Chess instance
     * @param botName Unique name for this bot (e.g., "TeamAlpha", "StudentBot1")
     */
    TournamentClient(Chess* game, const std::string& botName)
        : _game(game)
        , _botName(botName)
        , _socket(INVALID_SOCKET_VALUE)
        , _state(State::Disconnected)
        , _waitingForAI(false)
        , _moveReady(false)
#ifdef _WIN32
        , _wsaInitialized(false)
#endif
    {
#ifdef _WIN32
        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            _lastError = "WSAStartup failed: " + std::to_string(result);
            _state = State::Error;
        } else {
            _wsaInitialized = true;
        }
#endif
        addLog("TournamentClient created for bot: " + _botName);
    }

    /**
     * Destructor - Clean up socket resources
     */
    ~TournamentClient() {
        disconnect();
#ifdef _WIN32
        if (_wsaInitialized) {
            WSACleanup();
        }
#endif
    }

    // Prevent copying
    TournamentClient(const TournamentClient&) = delete;
    TournamentClient& operator=(const TournamentClient&) = delete;

    /**
     * Connect to the relay server
     * @param ip Server IP address (e.g., "127.0.0.1")
     * @param port Server port (e.g., 12345)
     * @return true if connection initiated successfully
     */
    bool connect(const std::string& ip, int port) {
        if (_state == State::Connected || _state == State::Connecting) {
            addLog("Already connected or connecting");
            return false;
        }

#ifdef _WIN32
        if (!_wsaInitialized) {
            addLog("Winsock not initialized");
            return false;
        }
#endif

        addLog("Connecting to " + ip + ":" + std::to_string(port));

        // Create socket
        _socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_socket == INVALID_SOCKET_VALUE) {
            _lastError = "Failed to create socket";
            addLog(_lastError);
            _state = State::Error;
            return false;
        }

        // Set up server address
        struct sockaddr_in serverAddr;
        std::memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(static_cast<uint16_t>(port));

        // Convert IP string to binary
        if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
            // Try hostname resolution
            struct hostent* host = gethostbyname(ip.c_str());
            if (host == nullptr) {
                _lastError = "Invalid address: " + ip;
                addLog(_lastError);
                CLOSE_SOCKET(_socket);
                _socket = INVALID_SOCKET_VALUE;
                _state = State::Error;
                return false;
            }
            std::memcpy(&serverAddr.sin_addr, host->h_addr_list[0], host->h_length);
        }

        // Connect (blocking for simplicity during initial connection)
        if (::connect(_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            _lastError = "Connection failed to " + ip + ":" + std::to_string(port);
            addLog(_lastError);
            CLOSE_SOCKET(_socket);
            _socket = INVALID_SOCKET_VALUE;
            _state = State::Error;
            return false;
        }

        // Set socket to non-blocking mode (CRITICAL for ImGui)
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(_socket, FIONBIO, &mode);
#else
        int flags = fcntl(_socket, F_GETFL, 0);
        fcntl(_socket, F_SETFL, flags | O_NONBLOCK);
#endif

        _state = State::Connected;
        _receiveBuffer.clear();
        addLog("Connected successfully!");

        // Send registration message
        std::string nameMsg = "NAME:" + _botName + "\n";
        sendRaw(nameMsg);
        addLog("Sent registration: " + _botName);

        return true;
    }

    /**
     * Disconnect from the server
     */
    void disconnect() {
        if (_socket != INVALID_SOCKET_VALUE) {
            CLOSE_SOCKET(_socket);
            _socket = INVALID_SOCKET_VALUE;
        }
        _state = State::Disconnected;
        _receiveBuffer.clear();
        _waitingForAI = false;
        _moveReady = false;
        addLog("Disconnected");
    }

    /**
     * Update - Call this every frame
     * Handles receiving data, processing FEN, running AI, and sending moves.
     */
    void update() {
        if (_state != State::Connected) {
            return;
        }

        // Receive any pending data
        receiveData();

        // Process complete messages
        processMessages();

        // If AI is computing and ready, send the move
        if (_moveReady && _game != nullptr) {
            sendAIMove();
            _moveReady = false;
            _waitingForAI = false;
        }
    }

    /**
     * Send a message to a target client
     * @param target Target client name (e.g., "ADMIN")
     * @param payload Message payload
     */
    void sendMessage(const std::string& target, const std::string& payload) {
        if (_state != State::Connected) {
            addLog("Cannot send - not connected");
            return;
        }

        std::string message = target + "|" + payload + "\n";
        sendRaw(message);
        addLog("Sent to " + target + ": " + payload);
    }

    /**
     * Set a callback for incoming messages
     * @param callback Function to call when a message arrives
     */
    void setMessageCallback(MessageCallback callback) {
        _messageCallback = callback;
    }

    // Getters
    State getState() const { return _state; }
    bool isConnected() const { return _state == State::Connected; }
    const std::string& getBotName() const { return _botName; }
    const std::string& getLastError() const { return _lastError; }
    const std::vector<std::string>& getLog() const { return _log; }

    /**
     * Get state as string for display
     */
    std::string getStateString() const {
        switch (_state) {
            case State::Disconnected: return "Disconnected";
            case State::Connecting: return "Connecting...";
            case State::Connected: return "Connected";
            case State::Error: return "Error: " + _lastError;
            default: return "Unknown";
        }
    }


    /**
     * Add entry to log (with timestamp)
     */
    void addLog(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        char timeStr[32];
        std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&time));

        std::string logEntry = std::string("[") + timeStr + "] " + message;
        _log.push_back(logEntry);

        // Keep log size bounded
        while (_log.size() > MAX_LOG_ENTRIES) {
            _log.erase(_log.begin());
        }

        // Also print to console for debugging
        std::cout << "[Tournament] " << message << std::endl;
    }
private:
    /**
     * Send raw data to socket
     */
    bool sendRaw(const std::string& data) {
        if (_socket == INVALID_SOCKET_VALUE) {
            return false;
        }

        int bytesSent = send(_socket, data.c_str(), static_cast<int>(data.length()), 0);
        if (bytesSent == SOCKET_ERROR_VALUE) {
            if (!WOULD_BLOCK_ERROR) {
                _lastError = "Send failed";
                addLog(_lastError);
                disconnect();
                _state = State::Error;
                return false;
            }
        }
        return true;
    }

    /**
     * Receive data from socket (non-blocking)
     */
void receiveData() {
    char buffer[4096];

    while (true) {
        int bytesReceived = recv(_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            _receiveBuffer += buffer;
        } else if (bytesReceived == 0) {
            addLog("Server closed connection");
            disconnect();
            return;
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINTR) {
                _lastError = "Receive error: " + std::to_string(err);
                addLog(_lastError);
                disconnect();
                _state = State::Error;
            }
#else
            if (!WOULD_BLOCK_ERROR) {
                _lastError = "Receive error";
                addLog(_lastError);
                disconnect();
                _state = State::Error;
            }
#endif
            break;
        }
    }
}
    /**
     * Process complete messages from buffer
     */
    void processMessages() {
        size_t pos;
        while ((pos = _receiveBuffer.find('\n')) != std::string::npos) {
            std::string message = _receiveBuffer.substr(0, pos);
            _receiveBuffer.erase(0, pos + 1);

            // Trim whitespace
            while (!message.empty() && (message.back() == '\r' || message.back() == ' ')) {
                message.pop_back();
            }

            if (message.empty()) {
                continue;
            }

            // Parse SENDER|PAYLOAD format
            size_t pipePos = message.find('|');
            if (pipePos == std::string::npos) {
                addLog("Invalid message format: " + message);
                continue;
            }

            std::string sender = message.substr(0, pipePos);
            std::string payload = message.substr(pipePos + 1);

            addLog("Received from " + sender + ": " + payload);

            // Handle comms check PING
            if (payload == "TEST:PING") {
                sendMessage(sender, "TEST:PONG");
                addLog("Responded to PING from " + sender);
                continue;
            }
            // Handle comms check FEN test
            if (payload.substr(0, 9) == "TEST:FEN:") {
                std::string testFen = payload.substr(9);
                addLog("Comms test: Calculating move for test position...");
                if (_game != nullptr) {
                    _game->setBoardFromFEN(testFen);
                    _game->updateAI();
                    BitMove move = _game->getLastAIMove();
                    if (move.piece != NoPiece) {
                        std::string moveStr = "TEST:MOVE:" + std::to_string(static_cast<int>(move.from)) +
                                              "," + std::to_string(static_cast<int>(move.to));
                        sendMessage(sender, moveStr);
                        addLog("Comms test: Sent test move " + moveStr);
                    } else {
                        sendMessage(sender, "TEST:ERROR:NoMove");
                    }
                }
                continue;
            }
            // Handle FEN messages
            if (payload.substr(0, 4) == "FEN:") {
                std::string fen = payload.substr(4);
                handleFEN(fen);
            }
            // Handle other server messages
            else if (sender == "SERVER") {
                // Server acknowledgments, errors, etc.
                addLog("Server: " + payload);
                // Also call callback for SERVER messages (e.g., CLIENTS list)
                if (_messageCallback) {
                    _messageCallback(sender, payload);
                }
            }
            // Call custom callback if set
            else if (_messageCallback) {
                _messageCallback(sender, payload);
            }
        }
    }

    /**
     * Handle incoming FEN position
     */
    void handleFEN(const std::string& fen);

    /**
     * Send the AI's calculated move
     */
    void sendAIMove();
};

// These implementations need Chess.h, so they're defined after including it
// In a .cpp file or after Chess class is fully defined

#ifdef TOURNAMENT_IMPLEMENTATION
#include "Chess.h"

void TournamentClient::handleFEN(const std::string& fen) {
    if (_game == nullptr) {
        addLog("ERROR: Game pointer is null");
        return;
    }

    addLog("Setting board from FEN: " + fen);

    // Set the board state from FEN
    _game->setBoardFromFEN(fen);

    // Run the AI to calculate a move
    addLog("Running AI...");
    _game->updateAI();

    // Mark that we have a move ready to send
    _moveReady = true;
}

void TournamentClient::sendAIMove() {
    if (_game == nullptr) {
        addLog("ERROR: Game pointer is null");
        return;
    }

    // Get the move calculated by AI
    BitMove move = _game->getLastAIMove();

    if (move.piece == NoPiece) {
        addLog("WARNING: No valid move from AI");
        // Send a forfeit or error message
        sendMessage("ADMIN", "ERROR:NoValidMove");
        return;
    }

    // Format: MOVE:srcIndex,dstIndex
    std::string moveStr = "MOVE:" + std::to_string(static_cast<int>(move.from)) +
                          "," + std::to_string(static_cast<int>(move.to));

    // Add flags if needed (for promotion, etc.)
    if (move.flags & IsPromotion) {
        moveStr += ",PROMO";
    }

    sendMessage("ADMIN", moveStr);
    addLog("Sent move: " + moveStr);
}

#endif // TOURNAMENT_IMPLEMENTATION


/**
 * DirectorClient
 * ==============
 * Extended tournament client for the Director (teacher) role.
 * Manages game flow, validates moves, and orchestrates matches.
 */
class DirectorClient : public TournamentClient {
public:
    struct MatchInfo {
        std::string whiteBotName;
        std::string blackBotName;
        bool gameInProgress;
        bool isWhiteTurn;
        std::string result;  // "", "WHITE", "BLACK", "DRAW"
        std::vector<std::string> moveHistory;
    };

    // Comms check status for each bot
    struct CommsStatus {
        bool pingReceived;
        bool moveTestPassed;
        std::string lastTestTime;
    };

private:
    MatchInfo _match;
    std::vector<std::string> _connectedBots;
    std::vector<std::string> _previousBots;  // Track previous list to detect new bots
    std::map<std::string, CommsStatus> _commsStatus;

public:
    /**
     * Constructor - Director always registers as "ADMIN"
     */
    DirectorClient(Chess* game)
        : TournamentClient(game, "ADMIN")
    {
        _match.gameInProgress = false;
        _match.isWhiteTurn = true;

        // Set up message handler for Director
        setMessageCallback([this](const std::string& sender, const std::string& payload) {
            handleBotMessage(sender, payload);
        });
    }

    /**
     * Start a new match between two bots
     */
    bool startMatch(const std::string& whiteBotName, const std::string& blackBotName);

    /**
     * Send FEN to the current player's bot
     */
    void sendFENToCurrentPlayer();

    /**
     * Handle incoming move from a bot
     */
    void handleBotMessage(const std::string& sender, const std::string& payload);

    /**
     * Run comms check on a specific bot
     */
    void runCommsCheck(const std::string& botName);

    /**
     * Validate and apply a move
     */
    bool validateAndApplyMove(int srcIndex, int dstIndex);

    /**
     * Manually override/fix game state (for Director use)
     */
    void manualMove(int srcIndex, int dstIndex);

    /**
     * End the current match
     */
    void endMatch(const std::string& result);

    // Getters
    const MatchInfo& getMatchInfo() const { return _match; }
    const std::vector<std::string>& getConnectedBots() const { return _connectedBots; }
    bool isGameInProgress() const { return _match.gameInProgress; }
    const std::map<std::string, CommsStatus>& getCommsStatus() const { return _commsStatus; }
    bool isBotVerified(const std::string& botName) const {
        auto it = _commsStatus.find(botName);
        return it != _commsStatus.end() && it->second.pingReceived && it->second.moveTestPassed;
    }
};

#ifdef TOURNAMENT_IMPLEMENTATION

bool DirectorClient::startMatch(const std::string& whiteBotName, const std::string& blackBotName) {
    if (!isConnected()) {
        return false;
    }

    // Reset game state
    if (_game != nullptr) {
        _game->stopGame();
        _game->setUpBoard();
    }

    _match.whiteBotName = whiteBotName;
    _match.blackBotName = blackBotName;
    _match.gameInProgress = true;
    _match.isWhiteTurn = true;
    _match.result = "";
    _match.moveHistory.clear();

    addLog("Starting match: " + whiteBotName + " (White) vs " + blackBotName + " (Black)");

    // Send initial FEN to white
    sendFENToCurrentPlayer();

    return true;
}

void DirectorClient::sendFENToCurrentPlayer() {
    if (_game == nullptr || !_match.gameInProgress) {
        return;
    }

    std::string fen = _game->getFEN();
    std::string targetBot = _match.isWhiteTurn ? _match.whiteBotName : _match.blackBotName;

    sendMessage(targetBot, "FEN:" + fen);
}

void DirectorClient::runCommsCheck(const std::string& botName) {
    if (!isConnected()) {
        return;
    }

    addLog(">>> Running comms check for: " + botName);

    // Initialize status
    _commsStatus[botName] = CommsStatus{false, false, ""};

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeStr[32];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&time));
    _commsStatus[botName].lastTestTime = timeStr;

    // Send PING
    sendMessage(botName, "TEST:PING");

    // Send test FEN (a simple position where there are obvious moves)
    sendMessage(botName, "TEST:FEN:rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
}

void DirectorClient::handleBotMessage(const std::string& sender, const std::string& payload) {
    // Track connected bots from server updates
    if (sender == "SERVER" && payload.substr(0, 8) == "CLIENTS:") {
        _previousBots = _connectedBots;  // Save previous list
        _connectedBots.clear();
        std::string clients = payload.substr(8);
        std::stringstream ss(clients);
        std::string bot;
        while (std::getline(ss, bot, ',')) {
            if (!bot.empty() && bot != "ADMIN") {
                _connectedBots.push_back(bot);
            }
        }

        // Check for newly connected bots and auto-run comms check
        for (const auto& bot : _connectedBots) {
            if (std::find(_previousBots.begin(), _previousBots.end(), bot) == _previousBots.end()) {
                // New bot detected!
                addLog("*** NEW BOT CONNECTED: " + bot + " ***");
                runCommsCheck(bot);
            }
        }
        return;
    }

    // Handle comms check PONG response
    if (payload == "TEST:PONG") {
        _commsStatus[sender].pingReceived = true;
        addLog("<<< PING OK from " + sender);
        return;
    }

    // Handle comms check MOVE response
    if (payload.substr(0, 10) == "TEST:MOVE:") {
        std::string moveData = payload.substr(10);
        int src = -1, dst = -1;
        if (sscanf(moveData.c_str(), "%d,%d", &src, &dst) == 2) {
            _commsStatus[sender].moveTestPassed = true;
            addLog("<<< MOVE TEST OK from " + sender + " (move: " + std::to_string(src) + "->" + std::to_string(dst) + ")");
            addLog("*** " + sender + " COMMS CHECK PASSED ***");
        }
        return;
    }

    // Handle comms check error
    if (payload.substr(0, 11) == "TEST:ERROR:") {
        _commsStatus[sender].moveTestPassed = false;
        addLog("<<< MOVE TEST FAILED from " + sender + ": " + payload.substr(11));
        return;
    }

    // Handle move from bot (game moves, not test moves)
    if (payload.substr(0, 5) == "MOVE:") {
        if (!_match.gameInProgress) {
            addLog("Ignoring move from " + sender + " - no game in progress");
            return;
        }

        // Check if it's from the correct bot
        std::string expectedBot = _match.isWhiteTurn ? _match.whiteBotName : _match.blackBotName;
        if (sender != expectedBot) {
            addLog("Ignoring move from " + sender + " - expected " + expectedBot);
            return;
        }

        // Parse MOVE:src,dst
        std::string moveData = payload.substr(5);
        int src = -1, dst = -1;
        if (sscanf(moveData.c_str(), "%d,%d", &src, &dst) == 2) {
            addLog("Received move from " + sender + ": " + std::to_string(src) + " -> " + std::to_string(dst));

            if (validateAndApplyMove(src, dst)) {
                // Check for game end
                if (_game != nullptr) {
                    Player* winner = _game->checkForWinner();
                    if (winner) {
                        endMatch(winner->playerNumber() == 0 ? "WHITE" : "BLACK");
                        return;
                    }
                    if (_game->checkForDraw()) {
                        endMatch("DRAW");
                        return;
                    }
                }

                // Switch turns and send FEN to next player
                _match.isWhiteTurn = !_match.isWhiteTurn;
                sendFENToCurrentPlayer();
            } else {
                addLog("!!! ILLEGAL MOVE from " + sender + ": " + std::to_string(src) + " -> " + std::to_string(dst));
            }
        }
    }
    // Handle errors from bots
    else if (payload.substr(0, 6) == "ERROR:") {
        addLog("Error from " + sender + ": " + payload.substr(6));
    }
}

bool DirectorClient::validateAndApplyMove(int srcIndex, int dstIndex) {
    if (_game == nullptr) {
        return false;
    }

    // Get the source and destination holders
    int srcX = srcIndex & 7;
    int srcY = srcIndex / 8;
    int dstX = dstIndex & 7;
    int dstY = dstIndex / 8;

    BitHolder& src = _game->getHolderAt(srcX, srcY);
    BitHolder& dst = _game->getHolderAt(dstX, dstY);

    Bit* piece = src.bit();
    if (piece == nullptr) {
        return false;
    }

    // Validate using the game's move validation
    if (!_game->canBitMoveFromTo(*piece, src, dst)) {
        return false;
    }

    // Apply the move
    dst.dropBitAtPoint(piece, ImVec2(0, 0));
    src.setBit(nullptr);
    _game->bitMovedFromTo(*piece, src, dst);

    // Record move
    _match.moveHistory.push_back(std::to_string(srcIndex) + "-" + std::to_string(dstIndex));

    return true;
}

void DirectorClient::manualMove(int srcIndex, int dstIndex) {
    validateAndApplyMove(srcIndex, dstIndex);
}

void DirectorClient::endMatch(const std::string& result) {
    _match.result = result;
    _match.gameInProgress = false;

    // Notify both bots
    sendMessage(_match.whiteBotName, "GAMEOVER:" + result);
    sendMessage(_match.blackBotName, "GAMEOVER:" + result);
}

#endif // TOURNAMENT_IMPLEMENTATION
