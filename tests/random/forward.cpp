import rstd.core;

using namespace rstd::prelude;
namespace random = rstd::random;

struct ForwardSource {
    using Error = u32;
    array<u64,3> words{u64(3),u64(),u64(1)};
    usize at{},limit{usize(3)};
    constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty,Error> {
        if(at==limit)return Err(u32(91));
        auto word=words[at++];
        for(usize i{};i<output.len();++i)output[i]=u8((word>>u64(i.to_primitive()*8)).to_primitive()&255);
        return Ok(empty{});
    }
};

constexpr auto forward_indices() -> bool {
    array<u8,4> values{u8(),u8(1),u8(2),u8(3)};ForwardSource source;
    if(random::shuffle_forward(values.as_mut_slice(),source).is_err() || source.at!=usize(3))return false;
    return values[usize()]==u8(3) && values[usize(1)]==u8(1) && values[usize(2)]==u8() && values[usize(3)]==u8(2);
}
static_assert(forward_indices());

struct ForwardMoveOnly {
    unsigned value{};
    ForwardMoveOnly()=default;
    explicit ForwardMoveOnly(unsigned id):value(id){}
    ForwardMoveOnly(const ForwardMoveOnly &)=delete;
    auto operator=(const ForwardMoveOnly &) -> ForwardMoveOnly & =delete;
    ForwardMoveOnly(ForwardMoveOnly &&other):value(other.value){other.value=99;}
    auto operator=(ForwardMoveOnly &&other) -> ForwardMoveOnly & {value=other.value;other.value=99;return *this;}
};

auto main() -> int {
    if(!forward_indices())return 1;
    array<u8,0> empty_values{};array<u8,1> single{u8(7)};ForwardSource none;none.limit=usize();
    if(random::shuffle_forward(empty_values.as_mut_slice(),none).is_err() || random::shuffle_forward(single.as_mut_slice(),none).is_err() || none.at!=usize() || single[usize()]!=u8(7))return 2;
    array<u8,4> partial{u8(),u8(1),u8(2),u8(3)};ForwardSource failure;failure.limit=usize(1);
    if(random::shuffle_forward(partial.as_mut_slice(),failure).unwrap_err()!=u32(91))return 3;
    u32 seen{};for(auto value:partial)seen|=u32(1)<<u64(u8(value).to_primitive());
    if(seen!=u32(15) || partial[usize()]!=u8(3) || partial[usize(3)]!=u8())return 4;
    array<ForwardMoveOnly,4> movable{ForwardMoveOnly(0),ForwardMoveOnly(1),ForwardMoveOnly(2),ForwardMoveOnly(3)};ForwardSource moves;
    if(random::shuffle_forward(movable.as_mut_slice(),moves).is_err() || movable[usize()].value!=3 || movable[usize(2)].value!=0)return 5;
    array<u8,4> backward{u8(),u8(1),u8(2),u8(3)};ForwardSource original;
    if(random::shuffle(backward.as_mut_slice(),original).is_err() || backward[usize()]!=u8(2) || backward[usize(1)]!=u8(1) || backward[usize(2)]!=u8() || backward[usize(3)]!=u8(3))return 6;
    return 0;
}
