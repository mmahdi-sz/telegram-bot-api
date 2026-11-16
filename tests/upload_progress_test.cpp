#include "telegram-bot-api/UploadProgress.h"
#include <cassert>
#include <iostream>
using namespace telegram_bot_api;
int main() {
  UploadProgressRegistry r;
  assert(!UploadProgressRegistry::valid_id(""));
  assert(!UploadProgressRegistry::valid_id("../x"));
  assert(!UploadProgressRegistry::valid_id(std::string(65, 'x')));
  assert(UploadProgressRegistry::valid_id("job_123-abc"));
  auto a = r.create("a", 0);
  auto b = r.create("b", 0);
  assert(a && b && !r.create("a", 1));
  assert(a->stage() == "queued");
  a->attach(7, 100, 0, false);
  b->attach(8, 200, 0, false);
  r.update(7, 100, 40, false);
  assert(a->uploaded == 40 && b->uploaded == 0);
  auto c = r.create("c", 0);
  c->attach(7, 100, 40, false);
  r.update(7, 100, 100, true);
  assert(a->stage() == "finalizing" && c->uploaded == 100);
  assert(!a->terminal());
  c->attach(7, 100, 100, false);
  assert(c->stage() == "finalizing" && !c->remote_complete);
  a->finish(UploadProgress::State::Completed, 0, 5);
  a->finish(UploadProgress::State::Failed, 500, 6);
  r.update(7, 100, 0, false);
  assert(a->stage() == "completed" && a->uploaded == 100);
  assert(c->uploaded == 0);  // A retry must not be hidden by monotonic clamping.
  b->finish(UploadProgress::State::Cancelled, 0, 6);
  r.update(8, 200, 90, false);
  assert(b->stage() == "cancelled" && b->uploaded == 0);
  c->finish(UploadProgress::State::Failed, 400, 7);
  assert(c->error_code == 400);
  auto unknown = r.create("unknown", 1);
  unknown->attach(9, 0, 0, false);
  assert(unknown->total == 0 && unknown->stage() == "uploading");
  assert(r.find("missing", 10) == nullptr);
  assert(r.find("a", 604) != nullptr);
  assert(r.find("a", 605) == nullptr);
  assert(r.find("unknown", 10000) != nullptr);  // Never expire active sends.
  UploadProgressRegistry other_bot;
  assert(!other_bot.find("unknown", 1));
  assert(other_bot.create("unknown", 1));
  UploadProgressRegistry full;
  for (size_t i = 0; i < UploadProgressRegistry::capacity; ++i) {
    assert(full.create(std::to_string(i), 0));
  }
  assert(!full.create("overflow", 1));
  full.find("0", 1)->finish(UploadProgress::State::Failed, 400, 1);
  assert(full.create("after-expiry", 601));
  std::cout << "upload progress regression tests passed\n";
}
