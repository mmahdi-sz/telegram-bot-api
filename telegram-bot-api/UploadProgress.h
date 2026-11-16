#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace telegram_bot_api {

// Owned by a single Client actor; bytes describe the main media file, not thumbnails or wire overhead.
struct UploadProgress {
  enum class State { Pending, Completed, Failed, Cancelled };
  State state = State::Pending;
  std::int32_t file_id = 0;
  std::int64_t chat_id = 0;
  std::int64_t message_id = 0;
  std::int64_t total = 0;
  std::int64_t uploaded = 0;
  bool remote_complete = false;
  bool cancelling = false;
  int error_code = 0;
  double started_at = 0;
  double finished_at = 0;

  bool terminal() const {
    return state != State::Pending;
  }
  std::string stage() const {
    switch (state) {
      case State::Completed:
        return "completed";
      case State::Failed:
        return "failed";
      case State::Cancelled:
        return "cancelled";
      default:
        if (cancelling)
          return "cancelling";
        if (file_id == 0)
          return "queued";
        return remote_complete || (total > 0 && uploaded >= total) ? "finalizing" : "uploading";
    }
  }
  void attach(std::int32_t id, std::int64_t size, std::int64_t bytes, bool complete) {
    if (!terminal()) {
      file_id = id;
      total = size;
      uploaded = bytes;
      remote_complete = complete;
    }
  }
  void finish(State outcome, int code, double now) {
    if (!terminal()) {
      state = outcome;
      cancelling = false;
      error_code = code;
      finished_at = now;
    }
  }
};

class UploadProgressRegistry {
 public:
  static constexpr std::size_t capacity = 1024;
  static constexpr double retention_seconds = 600;

  static bool valid_id(const std::string &id) {
    if (id.empty() || id.size() > 64) {
      return false;
    }
    for (auto c : id) {
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
        return false;
      }
    }
    return true;
  }
  std::shared_ptr<UploadProgress> create(const std::string &id, double now) {
    prune(now);
    if (!valid_id(id) || records_.size() >= capacity || records_.count(id)) {
      return nullptr;
    }
    auto record = std::make_shared<UploadProgress>();
    record->started_at = now;
    records_.emplace(id, record);
    return record;
  }
  std::shared_ptr<UploadProgress> find(const std::string &id, double now) {
    prune(now);
    auto it = records_.find(id);
    return it == records_.end() ? nullptr : it->second;
  }
  void update(std::int32_t id, std::int64_t size, std::int64_t bytes, bool complete) {
    for (auto &entry : records_) {
      if (entry.second->file_id == id && id != 0) {
        entry.second->attach(id, size, bytes, complete);
      }
    }
  }

 private:
  std::unordered_map<std::string, std::shared_ptr<UploadProgress>> records_;
  void prune(double now) {
    for (auto it = records_.begin(); it != records_.end();) {
      if (it->second->terminal() && now - it->second->finished_at >= retention_seconds) {
        it = records_.erase(it);
      } else {
        ++it;
      }
    }
  }
};

}  // namespace telegram_bot_api
