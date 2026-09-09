export module rstd:sys.args.windows;
import :ffi.os_str.encoding;
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

namespace rstd::sys::args::windows
{

// The program name has different quote rules from subsequent CRT arguments.
auto parse(slice<u16> line) -> Vec<Vec<u8>> {
    auto output = Vec<Vec<u8>>::make();
    if (line.is_empty()) return output;
    auto       index  = usize();
    auto       word   = Vec<u16>::make();
    auto       quoted = false;
    const auto space  = [](u16 value) {
        return value == u16(' ') || value == u16('\t');
    };
    while (index < line.len() && line[index] != u16()) {
        auto value = line[index];
        if (value == u16('"'))
            quoted = ! quoted;
        else if (space(value) && ! quoted)
            break;
        else
            word.emplace_back(value);
        ++index;
    }
    output.push(rstd::ffi::os_encoding::from_wide(word.as_slice()));
    while (index < line.len()) {
        while (index < line.len() && space(line[index])) ++index;
        if (index == line.len() || line[index] == u16()) break;
        word.clear();
        quoted = false;
        while (index < line.len() && line[index] != u16()) {
            auto value = line[index];
            if (space(value) && ! quoted) break;
            auto slashes = usize();
            while (index < line.len() && line[index] == u16('\\')) {
                ++slashes;
                ++index;
            }
            if (index < line.len() && line[index] == u16('"')) {
                for (auto count = usize(); count < slashes / usize(2); ++count)
                    word.push(u16('\\'));
                if (slashes % usize(2) != usize()) {
                    word.push(u16('"'));
                    ++index;
                } else if (quoted && index + usize(1) < line.len() &&
                           line[index + usize(1)] == u16('"')) {
                    word.push(u16('"'));
                    index += usize(2);
                } else {
                    quoted = ! quoted;
                    ++index;
                }
            } else {
                for (auto count = usize(); count < slashes; ++count) word.push(u16('\\'));
                if (index == line.len() || line[index] == u16() || (! quoted && space(line[index])))
                    break;
                word.emplace_back(line[index++]);
            }
        }
        output.push(rstd::ffi::os_encoding::from_wide(word.as_slice()));
    }
    return output;
}

} // namespace rstd::sys::args::windows
