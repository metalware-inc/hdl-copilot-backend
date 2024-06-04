#pragma once

#include <memory>
#include <string>

namespace metalware {
class PacketHandler;
class LanguageClient : public std::enable_shared_from_this<LanguageClient> {
  public:
    LanguageClient();
    ~LanguageClient();

    void handle_communication();
    [[nodiscard]] static bool send_packet(const std::string& packet);
    void setup();
  private:
    bool client_connected_;

    void receive_data();
    void process_data();
    void close_connection();

    int extract_header(std::string& header); // returns length of header
    static int extract_content_length(const std::string& header);

    std::string buffer_;
    std::shared_ptr<PacketHandler> packet_handler_;
};
}
