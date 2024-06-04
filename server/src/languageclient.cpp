#include "languageclient.hpp"

#include <chrono>
#include <iostream>
#include <regex>
#include <thread>

#include "packethandler.hpp"
#include "spdlog/spdlog.h"

using namespace metalware;

void LanguageClient::setup() {
  packet_handler_ = std::make_shared<PacketHandler>(weak_from_this());
}

LanguageClient::LanguageClient() : client_connected_(true) {}

LanguageClient::~LanguageClient() = default;

void LanguageClient::handle_communication() {
  while (client_connected_) {
    receive_data();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

bool LanguageClient::send_packet(const std::string& packet) {
  std::cout << packet << std::flush;
  return true;
}

void LanguageClient::receive_data() {
  while (true) {
    // Read Header
    std::string header, tmp;
    std::getline(std::cin, header);  // Read until \r
    std::getline(std::cin, tmp);

    if (header.empty() || std::cin.eof()) {
      spdlog::info("End of input stream or empty header");
      break;
    }

    int content_length = extract_content_length(header);

    // Read Content
    std::string content;
    content.reserve(content_length);
    char ch;
    while (content.length() < content_length && std::cin.get(ch)) {
      content += ch;
    }

    if (content.length() != content_length) {
      spdlog::error("Incomplete content received. Expected: {}, Received: {}",
          content_length,
          content.length());
      continue;
    }

    buffer_ = content;  // Assuming buffer_ is a std::string

    process_data();
  }
}

void LanguageClient::process_data() {
  try {
    auto json_content = nlohmann::json::parse(buffer_);
    if (!packet_handler_->handle_json_message(json_content)) {
      spdlog::error("Error handling packet: {}", json_content.dump().substr(0, 75));
    }
  } catch (const nlohmann::json::parse_error& e) {
    spdlog::error("Error parsing JSON: {}", e.what());
  }
  buffer_.clear();  // Clear buffer after processing
}

int LanguageClient::extract_content_length(const std::string& header) {
  std::regex content_length_regex(R"(Content-Length: (\d+))");
  std::smatch match;
  if (std::regex_search(header, match, content_length_regex) && match.size() > 1) {
    return std::stoi(match[1].str());
  }
  return -1;  // Or throw an exception if appropriate
}
