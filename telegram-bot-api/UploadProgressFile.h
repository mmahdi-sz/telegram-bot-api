#pragma once
#include "td/telegram/td_api.h"

namespace telegram_bot_api {

inline const td::td_api::file *get_upload_file(const td::td_api::MessageContent *content) {
  namespace td_api = td::td_api;
  const td_api::file *file = nullptr;
  switch (content->get_id()) {
    case td_api::messageVideo::ID:
      file = static_cast<const td_api::messageVideo *>(content)->video_->video_.get();
      break;
    case td_api::messageDocument::ID:
      file = static_cast<const td_api::messageDocument *>(content)->document_->document_.get();
      break;
    case td_api::messageAudio::ID:
      file = static_cast<const td_api::messageAudio *>(content)->audio_->audio_.get();
      break;
    case td_api::messageVoiceNote::ID:
      file = static_cast<const td_api::messageVoiceNote *>(content)->voice_note_->voice_.get();
      break;
    case td_api::messageVideoNote::ID:
      file = static_cast<const td_api::messageVideoNote *>(content)->video_note_->video_.get();
      break;
    case td_api::messageAnimation::ID:
      file = static_cast<const td_api::messageAnimation *>(content)->animation_->animation_.get();
      break;
    case td_api::messagePhoto::ID: {
      const auto &sizes = static_cast<const td_api::messagePhoto *>(content)->photo_->sizes_;
      td::int64 area = -1;
      for (const auto &size : sizes) {
        auto candidate = static_cast<td::int64>(size->width_) * size->height_;
        if (candidate > area) {
          area = candidate;
          file = size->photo_.get();
        }
      }
      break;
    }
    default:
      break;
  }
  return file;
}

}  // namespace telegram_bot_api
