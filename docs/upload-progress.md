# Local upload progress extension (experimental)

This fork is based on Bot API 10.3 commit `2efabc7` and TDLib `bc9c263e2bfee06aaab41e82db51a103376030bc`. It is an isolated build, not an installed replacement. TDLib itself is unchanged.

## API

All extension requests require `--local`, direct loopback access (`127.0.0.1` or `::1`), and the normal bot token. Forwarded requests are rejected. Bind the server to loopback; do not expose these methods through a reverse proxy.

Supply an optional `upload_id` on `sendVideo`, `sendDocument`, `sendAudio`, `sendVoice`, `sendPhoto`, `sendAnimation`, or `sendVideoNote`. IDs contain 1–64 ASCII letters, digits, `_` or `-`; generate a fresh random ID for each request. Existing requests without this field keep their behavior. Albums, business messages, edits, and stories are not supported by this extension.

Poll `POST /bot<TOKEN>/getUploadProgress` with `upload_id` in the JSON body, at about 1-second intervals. Example result:

```json
{"ok":true,"result":{"upload_id":"job_123","stage":"uploading","uploaded_bytes":1048576,"total_bytes":10485760,"size_known":true,"remote_complete":false,"elapsed_seconds":2.5,"error_code":0}}
```

Stages: `queued`, `uploading`, `finalizing`, `cancelling`, `completed`, `failed`, `cancelled`. Only `completed` means the original send method succeeded. `finalizing` means all main-file bytes are available remotely or TDLib reports the remote copy complete; the send is still pending. Thumbnail transfer and message finalization may still be running.

Bytes are TDLib `remote.uploaded_size`: remote-available media bytes, updated after acknowledged chunks. They are not HTTP multipart bytes, TCP bytes, or encryption/retransmission overhead. Cached Telegram files may already be complete without new network traffic. Values can reset on a retry: consumers must reset their speed estimator if bytes decrease. Unknown exact sizes are `total_bytes=0, size_known=false`; do not calculate a percentage or ETA in that case. For photos the largest image by pixel area is tracked.

`elapsed_seconds` starts when Bot API registers the parsed request and freezes on termination. It excludes Rust-to-local-server multipart ingestion and authorization wait. The future Rust integration must time the whole request independently for the final caption. Derive upload ETA from successive byte samples; it is always an estimate, and no transfer ETA predicts the final send acknowledgment.

A 404 can mean the multipart body has not reached the handler yet, the ID is unknown, the terminal record expired, or the server restarted. It never means success. On startup, state is empty. Terminal records are retained for 10 minutes and pruned on registry access. The registry is per bot and holds at most 1024 entries; active entries are not evicted. Duplicate IDs return 409 and a full registry returns 429.

## Cancellation

`POST /bot<TOKEN>/cancelUpload` with `upload_id` requests deletion of that request's *pending message* through TDLib. It does not cancel a shared file ID globally. A true response acknowledges the cancellation request; keep polling for the terminal state or await the original send response. If sending wins the race, the result may be `completed`.

Before TDLib assigns the pending message, cancellation returns 409: retry while the original send is pending. Unknown IDs return 404; already failed/completed requests return 409. Already cancelled requests are idempotent. Confirmed deletion marks progress cancelled and fails the original send with HTTP 400. Closing the HTTP connection alone is not a remote cancellation mechanism.

## Build and tests

```sh
cmake -S . -B build-progress -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' \
  -DGPERF_EXECUTABLE="$PWD/tools-local/install/bin/gperf" \
  -DTELEGRAM_BOT_API_PROGRESS_TESTS=ON
taskset -c 0-3 cmake --build build-progress --parallel 4
ctest --test-dir build-progress --output-on-failure
```

The core regression test covers request isolation, shared file IDs, unknown size, retry resets, finalization, immutable terminal states, retention, duplicates and capacity. API tests cover real Query completion/error handling, loopback/forwarded-peer checks, JSON output, and all supported media selectors. Client tests execute real handlers with injected TDLib results: concurrent requests, pre-send failures, downstream failures, cancellation confirmation/cleanup, and per-bot isolation. They do not contact Telegram or prove real network throughput.

Before deployment, test authenticated uploads on a dedicated test bot/chat: two concurrent different files; same file in concurrent sends; small/cached files; cancellation during transfer; restart and network failure. Do not start a second instance with the running bot's token/database. Then integrate the shared Rust transfer module and validate dev before production. No UI or compressor naming changes are part of this patch.
