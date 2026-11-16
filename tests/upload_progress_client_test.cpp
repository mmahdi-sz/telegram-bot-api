#include "td/actor/ConcurrentScheduler.h"
#include "td/utils/Time.h"
#include "telegram-bot-api/Client.h"
#include "telegram-bot-api/ClientParameters.h"
#include <cassert>
#include <iostream>

namespace telegram_bot_api {
class UploadProgressClientTest final : public td::Actor {
  static PromisedQueryPtr request(const std::string &method, const std::string &id, td::unique_ptr<Query> &response,
                                  const char *ip = "127.0.0.1", bool include_id = true) {
    td::vector<td::BufferSlice> buffers;
    buffers.emplace_back(method);
    auto method_slice = buffers.back().as_mutable_slice();
    td::vector<std::pair<td::MutableSlice, td::MutableSlice>> args;
    if (include_id) {
      buffers.emplace_back("upload_id");
      auto key = buffers.back().as_mutable_slice();
      buffers.emplace_back(id);
      args.emplace_back(key, buffers.back().as_mutable_slice());
    }
    td::IPAddress peer;
    assert(peer.init_host_port(td::CSlice(ip), 12345).is_ok());
    auto q = td::make_unique<Query>(std::move(buffers), "1:test", false, method_slice, std::move(args),
                                    td::vector<std::pair<td::MutableSlice, td::MutableSlice>>{},
                                    td::vector<td::HttpFile>{}, nullptr, peer, false);
    auto promise = td::PromiseCreator::lambda([&response](td::Result<td::unique_ptr<Query>> value) {
      assert(value.is_ok());
      response = value.move_as_ok();
    });
    return PromisedQueryPtr(q.release(), PromiseDeleter(std::move(promise)));
  }
  static td_api::object_ptr<td_api::file> file(int id, td::int64 size, td::int64 uploaded, bool complete = false) {
    return td_api::make_object<td_api::file>(
        id, size, size, td_api::make_object<td_api::localFile>(),
        td_api::make_object<td_api::remoteFile>("", "", !complete, complete, uploaded));
  }
  static std::shared_ptr<UploadProgress> pending(Client &client, const std::string &id, int file_id,
                                                 td::int64 message_id, td::unique_ptr<Query> &response) {
    auto q = request("sendVideo", id, response);
    auto progress = client.upload_progress_.create(id, td::Time::now());
    q->upload_progress = progress;
    auto query_id = client.get_send_message_query_id(std::move(q), false);
    auto message = td_api::make_object<td_api::message>();
    message->chat_id_ = 42;
    message->id_ = message_id;
    auto video = td_api::make_object<td_api::messageVideo>();
    video->video_ = td_api::make_object<td_api::video>();
    video->video_->video_ = file(file_id, 100, 0);
    message->content_ = std::move(video);
    client.yet_unsent_message_count_[42]++;
    client.on_sent_message(std::move(message), query_id);
    return progress;
  }
  static void poll(Client &client, const std::string &id, int code, const char *fragment = "",
                   const char *ip = "127.0.0.1") {
    td::unique_ptr<Query> response;
    client.on_cmd(request("getUploadProgress", id, response, ip));
    assert(response && response->http_status_code() == code);
    assert(response->answer().as_slice().str().find(fragment) != std::string::npos);
  }
  void start_up() final {
    {
      auto parameters = std::make_shared<ClientParameters>();
      parameters->local_mode_ = true;
      Client client({}, "1:test", false, 0, parameters, {});
      client.was_authorized_ = true;
      poll(client, "missing", 404);
      poll(client, "../bad", 400);
      poll(client, "missing", 403, "", "192.168.1.1");
      parameters->local_mode_ = false;
      poll(client, "missing", 403);
      parameters->local_mode_ = true;
      td::unique_ptr<Query> response;
      client.on_cmd(request("sendVideo", "invalid-media", response));
      assert(response && response->http_status_code() == 400);
      poll(client, "invalid-media", 200, "\"stage\":\"failed\"");
      client.on_cmd(request("sendVideo", "invalid-media", response));
      assert(response->http_status_code() == 409);
      client.on_cmd(request("sendVideo", "", response, "127.0.0.1", false));
      assert(response->http_status_code() == 400);
      assert(!response->upload_progress);  // Untracked callers retain the original behavior.
      client.on_cmd(request("sendMediaGroup", "unsupported", response));
      assert(response->http_status_code() == 400);
      auto queued = client.upload_progress_.create("queued", td::Time::now());
      client.on_cmd(request("cancelUpload", "queued", response));
      assert(response->http_status_code() == 409 && !queued->cancelling);
      td::unique_ptr<Query> first_response, second_response;
      auto first = pending(client, "first", 7, -100, first_response);
      auto second = pending(client, "second", 8, -101, second_response);
      client.on_update_file(file(7, 100, 40));
      poll(client, "first", 200, "\"uploaded_bytes\":40");
      poll(client, "second", 200, "\"uploaded_bytes\":0");
      client.on_update_file(file(7, 100, 100, true));
      poll(client, "first", 200, "\"stage\":\"finalizing\"");
      assert(!first_response);  // Remote file completion must not complete sendVideo.
      client.on_message_send_failed(42, -100, 0, td_api::make_object<td_api::error>(400, "Test downstream failure"));
      assert(first_response && first_response->http_status_code() == 400);
      poll(client, "first", 200, "\"stage\":\"failed\"");
      second->cancelling = true;  // Inject the deletion confirmation after a cancellation request.
      client.delete_message(42, -101, false);
      assert(second_response && second_response->http_status_code() == 400);
      poll(client, "second", 200, "\"stage\":\"cancelled\"");
      assert(client.pending_send_message_queries_.empty());
      assert(client.yet_unsent_messages_.empty());
      assert(client.yet_unsent_message_count_.empty());
      client.on_cmd(request("cancelUpload", "second", response));
      assert(response->http_status_code() == 200);
      client.on_cmd(request("cancelUpload", "first", response));
      assert(response->http_status_code() == 409);
      auto other_parameters = std::make_shared<ClientParameters>();
      other_parameters->local_mode_ = true;
      Client other({}, "2:test", false, 0, other_parameters, {});
      other.was_authorized_ = true;
      poll(other, "second", 404);
      queued->finish(UploadProgress::State::Failed, 500, td::Time::now());
    }
    std::cout << "real Client upload progress handler tests passed\n";
    td::Scheduler::instance()->finish();
    stop();
  }
};
}  // namespace telegram_bot_api

int main() {
  td::ConcurrentScheduler scheduler(3, 0);
  scheduler.create_actor_unsafe<telegram_bot_api::UploadProgressClientTest>(0, "UploadProgressTest").release();
  scheduler.start();
  while (scheduler.run_main(1)) {
  }
  scheduler.finish();
}
