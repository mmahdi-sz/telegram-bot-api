#pragma once
#include "td/utils/JsonBuilder.h"
#include "telegram-bot-api/UploadProgress.h"

namespace telegram_bot_api {

class JsonUploadProgress final : public td::Jsonable {
 public:
  JsonUploadProgress(const std::string &id, const UploadProgress &progress, double now)
      : id_(id), progress_(progress), now_(now) {
  }
  void store(td::JsonValueScope *scope) const {
    auto object = scope->enter_object();
    object("upload_id", id_);
    object("stage", progress_.stage());
    object("uploaded_bytes", static_cast<td::int64>(progress_.uploaded));
    object("total_bytes", static_cast<td::int64>(progress_.total));
    object("size_known", td::JsonBool(progress_.total > 0));
    object("remote_complete", td::JsonBool(progress_.remote_complete));
    object("elapsed_seconds", (progress_.terminal() ? progress_.finished_at : now_) - progress_.started_at);
    object("error_code", progress_.error_code);
  }

 private:
  const std::string &id_;
  const UploadProgress &progress_;
  double now_;
};

}  // namespace telegram_bot_api
