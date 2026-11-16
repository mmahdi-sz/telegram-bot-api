#include "telegram-bot-api/JsonUploadProgress.h"
#include "telegram-bot-api/Query.h"
#include "telegram-bot-api/UploadProgressFile.h"
#include <cassert>
#include <iostream>
using namespace telegram_bot_api;
namespace api = td::td_api;

static void check_query(const char *ip, bool local, bool success, bool forwarded = false) {
  td::IPAddress peer;
  assert(peer.init_host_port(td::CSlice(ip), 12345).is_ok());
  char method[] = "sendvideo";
  char header[] = "x-forwarded-for";
  char value[] = "127.0.0.1";
  td::vector<std::pair<td::MutableSlice, td::MutableSlice>> headers;
  if (forwarded)
    headers.emplace_back(td::MutableSlice(header, sizeof(header) - 1), td::MutableSlice(value, sizeof(value) - 1));
  Query q({}, "1:test", false, td::MutableSlice(method, sizeof(method) - 1), {}, std::move(headers), {}, nullptr, peer, false);
  assert(q.is_direct_loopback() == local);
  q.upload_progress = std::make_shared<UploadProgress>();
  auto p = q.upload_progress;
  if (success) {
    q.set_ok(td::BufferSlice("{}"));
    assert(p->stage() == "completed" && q.http_status_code() == 200);
  } else {
    q.set_error(400, td::BufferSlice("{}"));
    assert(p->stage() == "failed" && p->error_code == 400);
  }
  assert(p->finished_at >= p->started_at);
}

int main() {
  check_query("127.0.0.1", true, true);
  check_query("::1", true, false);
  check_query("192.168.1.1", false, false);
  check_query("203.0.113.1", false, false);
  check_query("127.0.0.1", false, false, true);
  auto video = api::make_object<api::messageVideo>();
  video->video_ = api::make_object<api::video>();
  video->video_->video_ = api::make_object<api::file>();
  video->video_->video_->id_ = 23;
  assert(get_upload_file(video.get())->id_ == 23);
  auto doc = api::make_object<api::messageDocument>();
  doc->document_ = api::make_object<api::document>();
  doc->document_->document_ = api::make_object<api::file>();
  assert(get_upload_file(doc.get()) == doc->document_->document_.get());
  auto audio = api::make_object<api::messageAudio>();
  audio->audio_ = api::make_object<api::audio>();
  audio->audio_->audio_ = api::make_object<api::file>();
  assert(get_upload_file(audio.get()) == audio->audio_->audio_.get());
  auto voice = api::make_object<api::messageVoiceNote>();
  voice->voice_note_ = api::make_object<api::voiceNote>();
  voice->voice_note_->voice_ = api::make_object<api::file>();
  assert(get_upload_file(voice.get()) == voice->voice_note_->voice_.get());
  auto note = api::make_object<api::messageVideoNote>();
  note->video_note_ = api::make_object<api::videoNote>();
  note->video_note_->video_ = api::make_object<api::file>();
  assert(get_upload_file(note.get()) == note->video_note_->video_.get());
  auto animation = api::make_object<api::messageAnimation>();
  animation->animation_ = api::make_object<api::animation>();
  animation->animation_->animation_ = api::make_object<api::file>();
  assert(get_upload_file(animation.get()) == animation->animation_->animation_.get());
  auto photo = api::make_object<api::messagePhoto>();
  photo->photo_ = api::make_object<api::photo>();
  assert(get_upload_file(photo.get()) == nullptr);
  for (int width : {90, 1280, 320}) {
    auto size = api::make_object<api::photoSize>();
    size->width_ = size->height_ = width;
    size->photo_ = api::make_object<api::file>();
    size->photo_->id_ = width;
    photo->photo_->sizes_.push_back(std::move(size));
  }
  assert(get_upload_file(photo.get())->id_ == 1280);
  auto text = api::make_object<api::messageText>();
  assert(get_upload_file(text.get()) == nullptr);
  UploadProgress p;
  p.started_at = 10;
  p.attach(1, 100, 50, false);
  const std::string id = "job_1";
  auto json = td::json_encode<td::string>(JsonUploadProgress(id, p, 12));
  assert(json.find("\"uploaded_bytes\":50") != std::string::npos);
  assert(json.find("\"stage\":\"uploading\"") != std::string::npos);
  assert(json.find("\"elapsed_seconds\":2") != std::string::npos);
  assert(json.find("chat_id") == std::string::npos && json.find("file_id") == std::string::npos);
  p.finish(UploadProgress::State::Cancelled, 0, 13);
  p.finish(UploadProgress::State::Failed, 400, 14);
  json = td::json_encode<td::string>(JsonUploadProgress(id, p, 100));
  assert(json.find("\"stage\":\"cancelled\"") != std::string::npos);
  assert(json.find("\"elapsed_seconds\":3") != std::string::npos);
  std::cout << "upload progress API boundary tests passed\n";
}
