module rstd;
import :async.debug.protocol;

using namespace rstd;
using namespace rstd::literals;
using ::alloc::string::String;
namespace rstd::async::debug::protocol
{
auto number(slice<u8> token) -> Option<uint64_t> {
    if (token.is_empty()) return None();
    uint64_t value {};
    for (auto character : token) {
        auto digit = character.to_primitive();
        if (digit < '0' || digit > '9' || value > (~uint64_t(0) - (digit - '0')) / 10)
            return None();
        value = value * 10 + digit - '0';
    }
    return Some(value);
}
auto parse_request(slice<u8> line) -> Result<Request, RequestError> {
    Option<uint64_t> id;
    auto             fail = [&](ref<str> message) -> Result<Request, RequestError> {
        return Err(RequestError { message, id });
    };
    if (line.is_empty()) return fail("invalid_request"_str);
    slice<u8> tokens[5] { line, line, line, line, line };
    size_t    count {}, begin {};
    for (size_t offset = 0; offset <= line.len().to_primitive(); ++offset) {
        if (offset < line.len().to_primitive() && line[usize(offset)] != u8(' ')) {
            auto character = line[usize(offset)].to_primitive();
            if (character < 0x21 || character > 0x7e) return fail("invalid_request"_str);
            continue;
        }
        if (offset == begin || count == 5) return fail("invalid_request"_str);
        tokens[count++] =
            slice<u8>::from_raw_parts(line.as_raw_ptr() + begin, usize(offset - begin));
        begin = offset + 1;
        if (count == 2) {
            id = number(tokens[1]);
            if (id.is_none() || *id == 0) {
                id = None();
                return fail("invalid_request"_str);
            }
        }
    }
    if (count < 3) return fail("invalid_request"_str);
    auto version = number(tokens[0]);
    if (version.is_none()) return fail("invalid_request"_str);
    if (*version != VERSION) return fail("unsupported_version"_str);
    Request request { *id };
    auto    command = str_::from_utf8(tokens[2]).unwrap();
    if (command == "hello"_str || command == "snapshot"_str) {
        if (count != 3) return fail("invalid_request"_str);
        request.command = command == "hello"_str ? Command::Hello : Command::Snapshot;
    } else if (command == "snapshot_page"_str || command == "task"_str || command == "events"_str) {
        if (count < 4 || (command != "events"_str && count != 4))
            return fail("invalid_request"_str);
        auto argument = number(tokens[3]);
        if (argument.is_none())
            return fail(command == "events"_str ? "invalid_cursor"_str : "invalid_request"_str);
        request.argument = *argument;
        request.command  = command == "snapshot_page"_str ? Command::SnapshotPage
                           : command == "task"_str        ? Command::Task
                                                          : Command::Events;
        if (count == 5) {
            request.limit = number(tokens[4]);
            if (request.limit.is_none() || *request.limit == 0) return fail("invalid_request"_str);
        }
    } else
        return fail("invalid_request"_str);
    return Ok(request);
}

class Response {
    String text_ = String::make();
    size_t limit_;
    bool   failed_ {};

public:
    explicit Response(size_t limit): limit_(limit) {}
    auto size() const -> size_t { return text_.len().to_primitive(); }
    auto fits(size_t bytes, size_t reserved = 0) const -> bool {
        return ! failed_ && bytes <= limit_ - size() && reserved <= limit_ - size() - bytes;
    }
    void raw(ref<str> text) {
        if (! fits(text.len().to_primitive())) {
            failed_ = true;
            return;
        }
        text_.push_str(text);
    }
    void ascii(char character) {
        if (! fits(1)) {
            failed_ = true;
            return;
        }
        text_.push_ascii(character);
    }
    void quoted(ref<str> value) {
        raw("\""_str);
        size_t begin {};
        auto   bytes = value->as_bytes();
        for (size_t offset = 0; offset < bytes.len().to_primitive(); ++offset) {
            auto character = bytes[usize(offset)].to_primitive();
            if (character >= 0x20 && character != '"' && character != '\\') continue;
            raw(ref<str>::from_raw_parts_unchecked(value.data() + begin, usize(offset - begin)));
            if (character == '"')
                raw("\\\""_str);
            else if (character == '\\')
                raw("\\\\"_str);
            else {
                constexpr char digits[] = "0123456789abcdef";
                raw("\\u00"_str);
                ascii(digits[character >> 4]);
                ascii(digits[character & 15]);
            }
            begin = offset + 1;
        }
        raw(ref<str>::from_raw_parts_unchecked(value.data() + begin, value.len() - usize(begin)));
        raw("\""_str);
    }
    void key(ref<str> key) {
        raw(","_str);
        quoted(key);
        raw(":"_str);
    }
    void text(ref<str> key, ref<str> value) {
        this->key(key);
        quoted(value);
    }
    void integer(uint64_t value) {
        auto text = rstd::format("{}", u64(value));
        raw(text.as_str());
    }
    void number(ref<str> key, uint64_t value) {
        this->key(key);
        integer(value);
    }
    void id(ref<str> key, uint64_t value) {
        this->key(key);
        raw("\""_str);
        integer(value);
        raw("\""_str);
    }
    void flag(ref<str> key, bool value) {
        this->key(key);
        raw(value ? "true"_str : "false"_str);
    }
    auto finish() -> Option<String> {
        if (failed_) return None();
        return Some(rstd::move(text_));
    }
};
auto envelope(Option<uint64_t> id, size_t bytes) -> Response {
    Response response(bytes ? bytes - 1 : 0);
    response.raw("{\"version\":"_str);
    response.integer(VERSION);
    response.raw(",\"request_id\":\""_str);
    if (id) response.integer(*id);
    response.raw("\""_str);
    return response;
}
auto error_response(RequestError error, size_t max_message_bytes) -> String {
    auto response = envelope(error.request_id, max_message_bytes);
    response.text("error"_str, error.message);
    if (error.message == "history_gap"_str) response.flag("resync_required"_str, true);
    response.raw("}"_str);
    return response.finish().unwrap_or(String::make());
}
void status(Response& response, const Status& status) {
    response.id("session"_str, status.session);
    response.flag("closed"_str, status.closed);
    response.flag("exhausted"_str, status.exhausted);
    response.flag("incomplete"_str, status.incomplete);
    response.id("omitted"_str, status.omitted);
    response.id("dropped_events"_str, status.dropped_events);
}
auto encode_record(const Record& record, size_t maximum) -> Option<String> {
    Response response(maximum);
    response.raw("{\"id\":\""_str);
    response.integer(record.id);
    response.raw("\""_str);
    response.id("parent"_str, record.parent);
    response.id("target"_str, record.target);
    response.text("kind"_str, into<ref<str>>(record.kind));
    response.text("state"_str, into<ref<str>>(record.state));
    response.id("version"_str, record.version);
    response.id("changed_ns"_str, record.changed_ns);
    response.text("name"_str, record.name.as_str());
    response.text("file"_str, record.file.as_str());
    response.number("line"_str, record.line);
    response.number("worker"_str, record.worker);
    response.flag("external"_str, record.external);
    response.flag("cancel_requested"_str, record.cancel_requested);
    response.flag("cancel_rejected"_str, record.cancel_rejected);
    response.flag("scheduler_terminal"_str, record.scheduler_terminal);
    response.flag("frame_cleaned"_str, record.frame_cleaned);
    response.flag("result_published"_str, record.result_published);
    response.flag("backend_terminal"_str, record.backend_terminal);
    response.text("outcome"_str, into<ref<str>>(record.outcome));
    response.flag("terminal"_str, record.terminal);
    response.flag("truncated"_str, record.truncated);
    response.raw("}"_str);
    return response.finish();
}
auto Session::page(uint64_t id) -> String {
    auto fail = [&](ref<str> error) {
        return error_response({ error, Some(id) }, options_.max_message_bytes);
    };
    const auto& snapshot = *snapshot_;
    auto        response = envelope(Some(id), options_.max_message_bytes);
    status(response, snapshot);
    response.id("snapshot_id"_str, snapshot_id_);
    response.id("cursor"_str, snapshot.cursor);
    response.id("observed_ns"_str, snapshot.observed_ns);
    response.raw(",\"records\":["_str);
    size_t count {};
    while (page_ < snapshot.records.len().to_primitive() && count < options_.page_records) {
        auto record = encode_record(snapshot.records[usize(page_)], options_.max_message_bytes);
        if (! record || ! response.fits(record->len().to_primitive() + (count ? 1 : 0), 15)) {
            if (! count) return fail("record_too_large"_str);
            break;
        }
        if (count) response.raw(","_str);
        response.raw(record->as_str());
        ++count;
        ++page_;
    }
    response.raw("]"_str);
    response.flag("more"_str, page_ < snapshot.records.len().to_primitive());
    response.raw("}"_str);
    auto encoded = response.finish();
    return encoded ? rstd::move(*encoded) : fail("record_too_large"_str);
}
auto Session::respond(const Request& request) -> String {
    auto id   = request.request_id;
    auto fail = [&](ref<str> error) {
        return error_response({ error, Some(id) }, options_.max_message_bytes);
    };
    if (snapshot_ && captured_.elapsed() >= options_.snapshot_lifetime)
        snapshot_ = None<Snapshot>();
    if (request.command == Command::Hello) {
        auto value = debug_.status();
        if (value.is_err()) return fail(into<ref<str>>(value.unwrap_err()));
        auto response = envelope(Some(id), options_.max_message_bytes);
        status(response, *value);
        response.number("max_message_bytes"_str, options_.max_message_bytes);
        response.number("max_snapshot_bytes"_str, options_.max_snapshot_bytes);
        response.number("page_records"_str, options_.page_records);
        response.raw(",\"runtime\":{\"multi_thread\":"_str);
        response.raw(value->runtime.multi_thread ? "true"_str : "false"_str);
        response.number("workers"_str, value->runtime.workers);
        response.number("max_blocking_threads"_str, value->runtime.max_blocking_threads);
        response.flag("io_enabled"_str, value->runtime.io_enabled);
        response.flag("time_enabled"_str, value->runtime.time_enabled);
        response.raw("}"_str);
        response.text("coverage"_str, coverage());
        response.raw("}"_str);
        auto encoded = response.finish();
        if (! encoded) return fail("record_too_large"_str);
        hello_ = true;
        return rstd::move(*encoded);
    }
    if (! hello_) return fail("hello_required"_str);
    if (request.command == Command::Snapshot || request.command == Command::Task) {
        snapshot_ = None<Snapshot>();
        auto view = request.command == Command::Snapshot
                        ? debug_.snapshot(options_.max_snapshot_bytes)
                        : debug_.task_snapshot(request.argument, options_.max_snapshot_bytes);
        if (view.is_err()) return fail(into<ref<str>>(view.unwrap_err()));
        if (snapshot_id_ == ~uint64_t(0)) return fail("snapshot_expired"_str);
        ++snapshot_id_;
        page_     = 0;
        captured_ = time::Instant::now();
        snapshot_ = Some(rstd::move(view).unwrap());
        return page(id);
    }
    if (request.command == Command::SnapshotPage) {
        if (! snapshot_ || request.argument != snapshot_id_) return fail("snapshot_expired"_str);
        return page(id);
    }
    auto limit = request.limit ? *request.limit : options_.page_records;
    if (! limit || limit > options_.page_records) return fail("invalid_request"_str);
    auto events = debug_.events(request.argument, static_cast<size_t>(limit));
    if (events.is_err()) return fail(into<ref<str>>(events.unwrap_err()));
    auto response = envelope(Some(id), options_.max_message_bytes);
    status(response, *events);
    response.raw(",\"events\":["_str);
    auto   cursor = request.argument;
    size_t count {};
    for (const auto& event : events->events) {
        auto     record = encode_record(event.record, options_.max_message_bytes);
        Response entry(options_.max_message_bytes);
        entry.raw("{\"sequence\":\""_str);
        entry.integer(event.sequence);
        entry.raw("\",\"record\":"_str);
        if (record) entry.raw(record->as_str());
        entry.raw("}"_str);
        auto encoded = record ? entry.finish() : None<String>();
        if (! encoded || ! response.fits(encoded->len().to_primitive() + (count ? 1 : 0), 39)) {
            if (! count) return fail("record_too_large"_str);
            break;
        }
        if (count) response.raw(","_str);
        response.raw(encoded->as_str());
        cursor = event.sequence;
        ++count;
    }
    response.raw("]"_str);
    response.id("next_cursor"_str, cursor);
    response.raw("}"_str);
    auto encoded = response.finish();
    return encoded ? rstd::move(*encoded) : fail("record_too_large"_str);
}
} // namespace rstd::async::debug::protocol
