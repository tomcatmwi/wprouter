#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <string>
#include <array>
#include <unordered_map>
#include <iostream>
#include <chrono>
#include <ctime>
#include <regex>
#include <cstring>

typedef websocketpp::server<websocketpp::config::asio> server_t;
typedef websocketpp::connection_hdl conn_hdl_t;

struct Client {
    conn_hdl_t hdl;
    std::string id;
    Client(conn_hdl_t h = conn_hdl_t(), const std::string& i = "") : hdl(h), id(i) {}
};

//  Version number and build time
const std::string version = "1.0 " __DATE__ " " __TIME__;

//  Logging flags
bool logging_enabled = false;
bool logging_verbose = false;

//  Maximum number of connections
unsigned int maxConnections = 10;

//  Returns a formatted timestamp
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::string ts = std::ctime(&tt);
    ts.pop_back(); // Remove newline
    return ts;
}

//  Prints a log line (if logging is enabled)
void log(const std::string& type, const std::string& msg) {
    if (logging_enabled) {
        std::cout << get_timestamp() << " [" << type << "] " << msg << std::endl;
    }
}

class WebSocketRouter {
private:
    server_t server;

    //  Clients connected, but not yet said hello    
    std::vector<Client> unconfirmed_clients;

    //  Clients connected with ID
    std::unordered_map<std::string, Client> clients{};

    //  Sends a Websocket message
    void send_message(conn_hdl_t hdl, const std::string& msg) {
        server.send(hdl, msg, websocketpp::frame::opcode::text);
        log("SENT", msg);
    }

    //  Sends an error message
    void send_error(conn_hdl_t hdl, const std::string& sender, const int code, const std::string& error) {
        std::string msg = "router::" + std::to_string(code) + "::::" + error;
        send_message(hdl, msg);
        log("ERROR", msg);
    }

    //  Validates a client ID
    bool is_valid_id(const std::string& id) {
        return !id.empty() && std::regex_match(id, std::regex("^[a-zA-Z0-9]+$"));
    }

    //  Removes a client
    void disconnect_client(const std::string& id, conn_hdl_t hdl) {
        if (clients.erase(id)) {
            server.close(hdl, websocketpp::close::status::normal, "Disconnected by router");
            return;
        }
        for (auto& uc : unconfirmed_clients) {
            if (!uc.hdl.expired() && uc.hdl.lock() == hdl.lock()) {
                uc = Client();
                server.close(hdl, websocketpp::close::status::normal, "Disconnected by router");
                return;
            }
        }
    }

    //  Parses and processes an incoming message
    void process_message(conn_hdl_t hdl, const std::string& msg) {
        log("RECV", msg);
        std::string sender_id;
        auto parts = split(msg, "::");
        if (parts.size() < 5) {
            send_error(hdl, parts.size() > 1 ? parts[1] : "", 2, "Message is incomplete");
            return;
        }

        //  Get message parts
        std::string recipient = parts[0];
        sender_id = parts[1];
        std::string expects_reply = parts[2];
        std::string reply_to = parts[3];
        std::string content = join(parts, "::", 4);

        //  Auto-register unconfirmed client
        //  If the sender's ID is unknown, save it
        bool is_unconfirmed = true;
        for (const auto& c : clients) {
            if (c.second.hdl.lock() == hdl.lock()) {
                is_unconfirmed = false;
                break;
            }
        }
        if (is_unconfirmed && !sender_id.empty()) {
            handle_hello(hdl, sender_id);
        }

        // Validate client IDs retrieved from the message
        
        if (!is_valid_id(sender_id)) {
            send_error(hdl, sender_id, 4, "Invalid sender id: \"" + sender_id + "\"");
            return;
        }

        if (sender_id == "router" || reply_to == "router") {
            send_error(hdl, sender_id, 6, "The router cannot be marked as sender, or be replied to.");
            return;
        }
        
        if (recipient.empty()) {
            send_error(hdl, sender_id, 5, "Recipient not specified");
            return;
        }

        //  Handle router commands
        if (recipient == "router") {
            handle_command(hdl, sender_id, parts);
            return;
        }

        //  Retrieve recipient and forward message
        std::string truncated_msg = join(parts, "::", 1);

        //  Send to all clients
        if (recipient == "*") {
            for (const auto& [id, client] : clients) {
                if (!client.hdl.expired() && client.id != sender_id) {
                    send_message(client.hdl, truncated_msg);
                }
            }
        } else 

        //  Send to single client
        if (clients.count(recipient)) {
            if (!clients[recipient].hdl.expired()) {
                send_message(clients[recipient].hdl, truncated_msg);
            }
        } 
        
        //  Client not found, send error
        else {
            send_error(hdl, sender_id, 3, "Client \"" + recipient + "\" is not connected to server");
        }
    }

    //  Handles commands for the router
    void handle_command(conn_hdl_t hdl, const std::string& sender, const std::vector<std::string>& parts) {
        if (parts.size() < 3) {
            send_error(hdl, sender, 1, "Message could not be parsed");
            return;
        }
        std::string command = parts[2];

        //  -------------------------------------------------------------------------------------------------------------------
        //  "hello"
        //  Identifies a new client
        //  -------------------------------------------------------------------------------------------------------------------
        if (command == "hello") {
            if (parts.size() < 4) {
                send_error(hdl, sender, 2, "Message is incomplete");
                return;
            }
            handle_hello(hdl, parts[3]);            
        } else 
        
        //  -------------------------------------------------------------------------------------------------------------------
        //  "ping"
        //  -------------------------------------------------------------------------------------------------------------------
        if (command == "ping") {
            send_message(hdl, "router::0::::pong");
            return;
        } else 
        
        //  -------------------------------------------------------------------------------------------------------------------
        //  "disconnect"
        //  Forces the router to drop a connected client
        //  -------------------------------------------------------------------------------------------------------------------
        if (command == "disconnect") {
            if (parts.size() < 4) {
                send_error(hdl, sender, 2, "Message is incomplete");
                return;
            }
            
            std::string target = parts[3];

            if (target == "*" || target == "") {

                if (target == "*") {
                    for (auto& [id, client] : clients) {
                        if (!client.hdl.expired()) {
                            server.close(client.hdl, websocketpp::close::status::normal, "Disconnected by router");
                        }
                    }
                    clients.clear();
                }

                for (auto& uc : unconfirmed_clients) {
                    if (!uc.hdl.expired()) {
                        server.close(uc.hdl, websocketpp::close::status::normal, "Disconnected by router");
                    }
                    uc = Client();
                }
                unconfirmed_clients.clear();

            } else if (!is_valid_id(target)) {
                send_error(hdl, sender, 4, "Invalid recipient id: \"" + target + "\"");
            } else if (clients.count(target)) {
                disconnect_client(target, clients[target].hdl);
                send_message(hdl, "router::0::::Client " + target + " disconnected.");
            } else {
                send_error(hdl, sender, 3, "Client \"" + target + "\" is not connected to server");
            }
        } else 
        
        //  -------------------------------------------------------------------------------------------------------------------
        //  "clients"
        //  Gets list of connected clients
        //  -------------------------------------------------------------------------------------------------------------------
        if (command == "clients") {
            if (parts.size() < 4) {
                send_error(hdl, sender, 2, "Message is incomplete");
                return;
            }
            std::string target = parts[3];

            //  Get all clients
            if (target == "*") {
                std::string list;
                for (const auto& [id, _] : clients) {
                    if (!list.empty()) list += ",";
                    list += id;
                }
                send_message(hdl, "router::0::::" + (list.empty() ? "None" : list));
            } else 
            
            //  Get number of confirmed and unconfirmed clients
            if (target == "") {
                send_message(hdl, "router::0::::" + std::to_string(clients.size()) + "," + std::to_string(unconfirmed_clients.size()));
            } else 
            
            //  Get if specific client is connected
            if (clients.count(target)) {
                send_message(hdl, "router::0::::" + target);
            } else {
                send_error(hdl, sender, 3, "Client \"" + target + "\" is not connected to server");
            }
        } else 
        
        //  -------------------------------------------------------------------------------------------------------------------
        //  "version"
        //  Returns version string
        //  -------------------------------------------------------------------------------------------------------------------
        if (command == "version") {
            send_message(hdl, "router::0::::" + version);
        } else {
            send_error(hdl, sender, 3, "Invalid command: \"" + command + "\"");
        }
    }

    //  Registers a new client
    void handle_hello(conn_hdl_t hdl, const std::string& id) {
        if (!is_valid_id(id)) {
            send_error(hdl, id, 4, "Invalid sender id: \"" + id + "\"");
            return;
        }

        //  Enforce connection limit
        if (unconfirmed_clients.size() + clients.size() >= maxConnections) {
            send_error(hdl, id, 7, "Router is full");
            server.close(hdl, websocketpp::close::status::normal, "Router full");
            return;
        }

        for (auto it = unconfirmed_clients.begin(); it != unconfirmed_clients.end(); ++it) {
            if (!it->hdl.expired() && it->hdl.lock() == hdl.lock()) {
                if (clients.count(id)) {
                    disconnect_client(id, clients[id].hdl);
                }
                clients[id] = std::move(*it);
                unconfirmed_clients.erase(it);
                return;
            }
        }

        send_message(hdl, "router::0::::hello " + id);
    }

    //  String splitter
    std::vector<std::string> split(const std::string& s, const std::string& delim) {
        std::vector<std::string> parts;
        size_t pos = 0, prev = 0;
        while ((pos = s.find(delim, prev)) != std::string::npos) {
            parts.push_back(s.substr(prev, pos - prev));
            prev = pos + delim.length();
        }
        parts.push_back(s.substr(prev));
        return parts;
    }

    std::string join(const std::vector<std::string>& parts, const std::string& delim, size_t start) {
        std::string result;
        for (size_t i = start; i < parts.size(); ++i) {
            if (i > start) result += delim;
            result += parts[i];
        }
        return result;
    }

//  Class definition
public:
    WebSocketRouter(int port) {
        server.init_asio();

        //  STFU to websocketpp
        if (!logging_verbose) {
            server.clear_access_channels(websocketpp::log::alevel::all);
            server.clear_error_channels(websocketpp::log::elevel::all);
        }

        //  Connection open handler
        server.set_open_handler([this](conn_hdl_t hdl) {
            if (unconfirmed_clients.size() + clients.size() >= maxConnections) {
                server.close(hdl, websocketpp::close::status::normal, "Router full");
                log("ERROR", "Connection rejected: Router is full");
                return;
            }
            unconfirmed_clients.emplace_back(hdl);
        });

        //  Connection close event handler
        server.set_close_handler([this](conn_hdl_t hdl) {
            // Remove from unconfirmed_clients
            for (auto it = unconfirmed_clients.begin(); it != unconfirmed_clients.end(); ++it) {
                if (!it->hdl.expired() && it->hdl.lock() == hdl.lock()) {
                    unconfirmed_clients.erase(it);
                    return; // only in one place at a time
                }
            }

            // Remove from confirmed clients
            for (auto it = clients.begin(); it != clients.end();) {
                if (!it->second.hdl.expired() && it->second.hdl.lock() == hdl.lock()) {
                    it = clients.erase(it);
                } else {
                    ++it;
                }
            }
        });

        //  Message event handler
        server.set_message_handler([this](conn_hdl_t hdl, server_t::message_ptr msg) {
            process_message(hdl, msg->get_payload());
        });

        server.listen(port);
        
        server.start_accept();
    }

    void run() {
        server.run();
    }
};

int main(int argc, char* argv[]) {

    int port = 8080;

    //  Analyze command line
    for (int i = 1; i < argc; ++i) {

        //  Help
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            const std::string text =
                "An ultralight HTTP only Websocket router\n\n"
                "--port, -p <port>      Port number. Default is " + std::to_string(port) + "\n"
                "--connections,\n"
                "  -c <connections>     Maximum number of Websocket clients. Default is " + std::to_string(maxConnections) + "\n"
                "--log, -l              Logging on\n"
                "--verbose              Verbose logging (enables websocketpp messages)\n"
                "--version, -v          Version\n"
                "--help, -h             This text\n"
                "\n";

            std::cout << text << std::endl;                            
            return 0;
        }

        //  Version
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::cout << "Ultralight Websocket Router " + version << std::endl; 
            return 0;
        }        

        //  Logging on/off
        if (std::strcmp(argv[i], "--log") == 0 || std::strcmp(argv[i], "-l") == 0) {
            logging_enabled = true;
        }

        //  Verbose loggig on/off
        if (std::strcmp(argv[i], "--verbose") == 0) {
            logging_enabled = true;
            logging_verbose = true;
        }

        //  Websocket port number
        if (i > 0 && (std::strcmp(argv[i-1], "--port") == 0 || std::strcmp(argv[i-1], "-p") == 0) && std::regex_match(argv[i], std::regex("^[0-9]+$"))) {
            port = std::stoi(argv[i]);
            if (port <= 0) {
                std::cout << "Invalid port number" << std::endl; 
                return 0;
            }
        }
    }

    std::cout << "Starting Websocket server on port " + std::to_string(port) << std::endl; 

    if (!logging_enabled)
        std::cout << "Logging is off" << std::endl; 

    WebSocketRouter router(port);

    router.run();
    return 0;
}