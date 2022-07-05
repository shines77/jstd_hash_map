
/************************************************************************************

  CC BY-SA 4.0 License

  Copyright (c) 2018-2022 XiongHui Guo (gz_shines@msn.com)

  https://github.com/shines77/jstd_hash_map
  https://gitee.com/shines77/jstd_hash_map

*************************************************************************************

  CC Attribution-ShareAlike 4.0 International

  https://creativecommons.org/licenses/by-sa/4.0/deed.en

  You are free to:

    1. Share -- copy and redistribute the material in any medium or format.

    2. Adapt -- remix, transforn, and build upon the material for any purpose,
    even commerically.

    The licensor cannot revoke these freedoms as long as you follow the license terms.

  Under the following terms:

    * Attribution -- You must give appropriate credit, provide a link to the license,
    and indicate if changes were made. You may do so in any reasonable manner,
    but not in any way that suggests the licensor endorses you or your use.

    * ShareAlike -- If you remix, transform, or build upon the material, you must
    distribute your contributions under the same license as the original.

    * No additional restrictions -- You may not apply legal terms or technological
    measures that legally restrict others from doing anything the license permits.

  Notices:

    * You do not have to comply with the license for elements of the material
    in the public domain or where your use is permitted by an applicable exception
    or limitation.

    * No warranties are given. The license may not give you all of the permissions
    necessary for your intended use. For example, other rights such as publicity,
    privacy, or moral rights may limit how you use the material.

************************************************************************************/

#pragma once

#include <memory.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <cstdint>
#include <cstddef>      // For std::ptrdiff_t, std::size_t
#include <cstdbool>
#include <cassert>
#include <cmath>        // For std::ceil()
#include <memory>       // For std::swap(), std::pointer_traits<T>
#include <limits>       // For std::numeric_limits<T>
#include <cstring>      // For std::memset(), std::memcpy()
#include <vector>
#include <utility>      // For std::pair<First, Second>, std::integer_sequence<T...>
#include <tuple>        // For std::tuple<Ts...>
#include <initializer_list>
#include <algorithm>    // For std::max(), std::min()
#include <type_traits>
#include <stdexcept>

#include <nmmintrin.h>
#include <immintrin.h>

#include "jstd/type_traits.h"
#include "jstd/iterator.h"
#include "jstd/utility.h"
#include "jstd/support/BitUtils.h"
#include "jstd/support/Power2.h"
#include "jstd/support/BitVec.h"

#ifdef _MSC_VER
#ifndef __SSE2__
#define __SSE2__
#endif

#ifndef __SSSE3__
#define __SSSE3__
#endif
#endif // _MSC_VER

namespace jstd {

template < typename Key, typename Value,
           typename Hash = std::hash<typename std::remove_cv<Key>::type>,
           typename KeyEqual = std::equal_to<typename std::remove_cv<Key>::type>,
           typename Allocator = std::allocator<std::pair<typename std::remove_const<typename std::remove_cv<Key>::type>::type,
                                                         typename std::remove_cv<Value>::type>> >
class robin32_hash_map {
public:
    typedef typename std::remove_cv<Key>::type      key_type;
    typedef typename std::remove_cv<Value>::type    mapped_type;

    typedef std::pair<key_type, mapped_type>        value_type;
    typedef std::pair<key_type, mapped_type>        mutable_value_type;

    typedef Hash                                    hasher;
    typedef KeyEqual                                key_equal;
    typedef Allocator                               allocator_type;
    typedef typename Hash::result_type              hash_result_t;

    typedef std::size_t                             size_type;
    typedef std::intptr_t                           ssize_type;
    typedef std::size_t                             hash_code_t;
    typedef robin32_hash_map<Key, Value, Hash, KeyEqual, Allocator>
                                                    this_type;

    static constexpr bool kUseIndexSalt = false;

    static constexpr size_type npos = size_type(-1);

    static constexpr size_type kControlHashMask = 0x000000FFul;
    static constexpr size_type kControlShift    = 8;

    static constexpr size_type kGroupBits   = 5;
    static constexpr size_type kGroupWidth  = size_type(1) << kGroupBits;
    static constexpr size_type kGroupMask   = kGroupWidth - 1;
    static constexpr size_type kGroupShift  = kControlShift + kGroupBits;

    // kMinimumCapacity must be >= 2
    static constexpr size_type kMinimumCapacity = 4;
    // kDefaultCapacity must be >= kMinimumCapacity
    static constexpr size_type kDefaultCapacity = 4;

    static constexpr float kMinLoadFactor = 0.2f;
    static constexpr float kMaxLoadFactor = 0.8f;

    // Must be kMinLoadFactor <= loadFactor <= kMaxLoadFactor
    static constexpr float kDefaultLoadFactor = 0.5f;

    static constexpr size_type kLoadFactorAmplify = 65536;
    static constexpr std::uint32_t kDefaultLoadFactorInt =
            std::uint32_t(kDefaultLoadFactor * kLoadFactorAmplify);
    static constexpr std::uint32_t kDefaultLoadFactorRevInt =
            std::uint32_t(1.0f / kDefaultLoadFactor * kLoadFactorAmplify);

#if defined(__GNUC__) || (defined(__clang__) && !defined(_MSC_VER))
    static constexpr bool isGccOrClang = true;
#else
    static constexpr bool isGccOrClang = false;
#endif
    static constexpr bool isPlaneKeyHash = isGccOrClang &&
                                           std::is_same<Hash, std::hash<key_type>>::value &&
                                          (std::is_arithmetic<key_type>::value ||
                                           std::is_enum<key_type>::value);

    static constexpr bool is_slot_trivial_copyable =
            (std::is_trivially_copyable<value_type>::value ||
            (std::is_trivially_copyable<key_type>::value &&
             std::is_trivially_copyable<mapped_type>::value) ||
            (std::is_scalar<key_type>::value && std::is_scalar<mapped_type>::value));

    static constexpr bool is_slot_trivial_destructor =
            (std::is_trivially_destructible<value_type>::value ||
            (std::is_trivially_destructible<key_type>::value &&
             std::is_trivially_destructible<mapped_type>::value) ||
           ((std::is_arithmetic<key_type>::value || std::is_enum<key_type>::value) &&
            (std::is_arithmetic<mapped_type>::value || std::is_enum<mapped_type>::value)));

    static constexpr bool kIsCompatibleLayout =
            std::is_same<value_type, mutable_value_type>::value ||
            is_compatible_layout<value_type, mutable_value_type>::value;

    static constexpr std::uint8_t kEmptyEntry   = 0b11111111;   
    static constexpr std::uint8_t kEndOfMark    = 0b11111110;
    static constexpr std::uint8_t kUnusedMask   = 0b10000000;
    static constexpr std::uint8_t kHash2Mask    = 0b11111111;

    static constexpr std::uint32_t kFullMask32  = 0xFFFFFFFFul;

    static constexpr std::uint64_t kEmptyEntry64   = 0xFFFFFFFFFFFFFFFFull;
    static constexpr std::uint64_t kEndOfMark64    = 0xFEFEFEFEFEFEFEFEull;
    static constexpr std::uint64_t kUnusedMask64   = 0x8080808080808080ull;

    struct control_data {
        std::uint8_t distance;
        std::uint8_t hash;

        control_data() noexcept {
        }

        ~control_data() = default;

        bool isEmpty() const {
            return (this->distance == kEmptyEntry);
        }

        bool isEndOf() const {
            return (this->distance == kEndOfMark);
        }

        bool isUsed() const {
            return (this->distance < kEmptyEntry);
        }

        static bool isUsed(std::uint8_t tag) {
            return (tag < kEmptyEntry);
        }

        bool isUnused() const {
            return (this->distance >= kEmptyEntry);
        }

        static bool isUnused(std::uint8_t tag) {
            return (tag >= kEmptyEntry);
        }

        void setHash(std::uint8_t ctrl_hash) {
            this->hash = ctrl_hash;
        }

        void setEmpty() {
            this->distance = kEmptyEntry;
        }

        void setEndOf() {
            this->distance = kEndOfMark;
        }

        void setDistance(std::uint8_t distance) {
            assert(distance < kEndOfMark);
            this->distance = distance;
        }

        void setUsed(std::uint8_t ctrl_hash, std::uint8_t distance) {
            this->setHash(ctrl_hash);
            this->setDistance(distance);
        }
    };

    typedef control_data  control_type;

#ifdef __AVX2__

    template <typename T>
    struct MatchMask2 {
        typedef T mask32_type;
        mask32_type maskHash;
        mask32_type maskEmpty;

        MatchMask2(mask32_type maskHash, mask32_type maskEmpty)
            : maskHash(maskHash), maskEmpty(maskEmpty) {
        }
        ~MatchMask2() = default;
    };

    template <typename T>
    struct MatchMask3 {
        typedef T mask32_type;
        mask32_type maskHash;
        mask32_type maskEmpty;
        mask32_type maskDistance;

        MatchMask3(mask32_type maskHash, mask32_type maskEmpty, mask32_type maskDistance)
            : maskHash(maskHash), maskEmpty(maskEmpty), maskDistance(maskDistance) {
        }
        ~MatchMask3() = default;
    };

    template <typename T>
    struct BitMask256_AVX2 {
        typedef T           value_type;
        typedef T *         pointer;
        typedef const T *   const_pointer;
        typedef T &         reference;
        typedef const T &   const_reference;

        typedef std::uint32_t bitmask_type;

        void clear(pointer data) {
            this->template fillAll16<kEmptyEntry>(data);
        }

        void setAllZeros(pointer data) {
            __m256i zero_bits = _mm256_setzero_si256();
            _mm256_storeu_si256((__m256i *)data, zero_bits);
        }

        template <std::uint16_t ControlTag>
        void fillAll16(pointer data) {
            __m256i tag_bits = _mm256_set1_epi16(ControlTag);
            _mm256_storeu_si256((__m256i *)data, tag_bits);
        }

        __m256i matchControlTag256(const_pointer data, std::uint16_t control_tag) const {
            __m256i ctrl_bits = _mm256_loadu_si256((const __m256i *)data);
            __m256i tag_bits  = _mm256_set1_epi16(control_tag);
            __m256i match_mask = _mm256_cmpeq_epi16(ctrl_bits, tag_bits);
            return match_mask;
        }

        std::uint32_t matchControlTag(const_pointer data, std::uint16_t control_tag) const {
            __m256i ctrl_bits = _mm256_loadu_si256((const __m256i *)data);
            __m256i tag_bits  = _mm256_set1_epi16(control_tag);
            __m256i match_mask = _mm256_cmpeq_epi16(ctrl_bits, tag_bits);
            std::uint32_t mask = (std::uint32_t)_mm256_movemask_epi8(match_mask);
            return mask;
        }

        std::uint32_t matchLowControlTag(const_pointer data, std::uint16_t control_tag) const {
            const __m256i kLowMask16  = _mm256_set1_epi16((short)0x00FF);
            __m256i ctrl_bits = _mm256_loadu_si256((const __m256i *)data);
            __m256i tag_bits  = _mm256_set1_epi16(control_tag);
            __m256i low_bits  = _mm256_and_si256(ctrl_bits, kLowMask16);
            __m256i match_mask = _mm256_cmpeq_epi16(low_bits, tag_bits);
            std::uint32_t mask = (std::uint32_t)_mm256_movemask_epi8(match_mask);
            return mask;
        }

        std::uint32_t matchHighControlTag(const_pointer data, std::uint16_t control_tag) const {
            const __m256i kHighMask16 = _mm256_set1_epi16((short)0xFF00);
            __m256i ctrl_bits = _mm256_loadu_si256((const __m256i *)data);
            __m256i tag_bits  = _mm256_set1_epi16(control_tag);            
            __m256i high_bits = _mm256_and_si256(ctrl_bits, kHighMask16);
            __m256i match_mask = _mm256_cmpeq_epi16(high_bits, tag_bits);
            std::uint32_t mask = (std::uint32_t)_mm256_movemask_epi8(match_mask);
            return mask;
        }

        MatchMask2<std::uint32_t>
        matchHashAndEmpty(const_pointer data, std::uint16_t ctrl_hash) const {
            const __m256i kLowMask16  = _mm256_set1_epi16((short)0x00FF);
            const __m256i kHighMask16 = _mm256_set1_epi16((short)0xFF00);
            __m256i ctrl_bits  = _mm256_loadu_si256((const __m256i *)data);
            __m256i hash_bits  = _mm256_set1_epi16(ctrl_hash);
            __m256i empty_bits = _mm256_set1_epi16(kEmptyEntry);
            __m256i low_bits   = _mm256_and_si256(ctrl_bits, kLowMask16);
            __m256i high_bits  = _mm256_and_si256(ctrl_bits, kHighMask16);
            __m256i empty_mask = _mm256_cmpeq_epi16(low_bits, empty_bits);
            __m256i match_mask = _mm256_cmpeq_epi16(high_bits, hash_bits);
            __m256i result_mask = _mm256_andnot_si256(empty_mask, match_mask);
            std::uint32_t maskEmpty = (std::uint32_t)_mm256_movemask_epi8(empty_mask);
            std::uint32_t maskHash  = (std::uint32_t)_mm256_movemask_epi8(result_mask);
            return { maskHash, maskEmpty };
        }

        MatchMask3<std::uint32_t>
        matchHashAndDistance(const_pointer data, std::uint16_t ctrl_hash, std::uint16_t distance) const {
            assert((distance & std::uint16_t(0x03)) == 0);
            const __m256i kLowMask16  = _mm256_set1_epi16((short)0x00FF);
            const __m256i kHighMask16 = _mm256_set1_epi16((short)0xFF00);
            const __m256i kDistance16[4] = {
                _mm256_setr_epi16(0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
                                  0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F),
                _mm256_setr_epi16(0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x0000, 0x0001, 0x0002, 0x0003,
                                  0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B),
                _mm256_setr_epi16(0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF,
                                  0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007),
                _mm256_setr_epi16(0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x00FF,
                                  0x00FF, 0x00FF, 0x00FF, 0x00FF, 0x0000, 0x0001, 0x0002, 0x0003),
            };
            const __m256i & dist_base = kDistance16[(distance % kGroupWidth) / 4];
            __m256i ctrl_bits  = _mm256_loadu_si256((const __m256i *)data);
            __m256i dist_plus  = _mm256_set1_epi16((short)distance);
            __m256i empty_bits = _mm256_set1_epi16(kEmptyEntry);
            __m256i hash_bits  = _mm256_set1_epi16(ctrl_hash);
            __m256i dist_index = _mm256_adds_epi16(dist_base, dist_plus);
            __m256i low_bits   = _mm256_and_si256(ctrl_bits, kLowMask16);
            __m256i high_bits  = _mm256_and_si256(ctrl_bits, kHighMask16);
            __m256i empty_mask = _mm256_cmpeq_epi16(low_bits, empty_bits);
            __m256i dist_mask  = _mm256_cmpgt_epi16(low_bits, dist_index);
                    empty_mask = _mm256_or_si256(empty_mask, dist_mask);
            __m256i match_mask = _mm256_cmpeq_epi16(high_bits, hash_bits);
            __m256i result_mask = _mm256_andnot_si256(empty_mask, match_mask);
            std::uint32_t maskInstance = (std::uint32_t)_mm256_movemask_epi8(dist_mask);
            std::uint32_t maskEmpty    = (std::uint32_t)_mm256_movemask_epi8(empty_mask);
            std::uint32_t maskHash     = (std::uint32_t)_mm256_movemask_epi8(result_mask);
            return { maskHash, maskEmpty, maskInstance };
        }

        std::uint32_t matchHash(const_pointer data, std::uint16_t control_hash) const {
            control_hash <<= 8;
            return this->matchHighControlTag(data, control_hash);
        }

        std::uint32_t matchEmpty(const_pointer data) const {
            return this->matchLowControlTag(data, kEmptyEntry);
        }

        std::uint32_t matchEmptyOrZero(const_pointer data) const {
            const __m256i kLowMask16 = _mm256_set1_epi16((short)0x00FF);
            __m256i ctrl_bits  = _mm256_loadu_si256((const __m256i *)data);
            __m256i empty_bits = _mm256_set1_epi16(kEmptyEntry);
            __m256i zero_bits  = _mm256_setzero_si256();
            __m256i low_bits = _mm256_and_si256(ctrl_bits, kLowMask16);
            __m256i match_mask = _mm256_cmpeq_epi16(low_bits, empty_bits);
            __m256i zero_mask  = _mm256_cmpeq_epi16(low_bits, zero_bits);
                    match_mask = _mm256_and_si256(match_mask, zero_mask);
            std::uint32_t mask = (std::uint32_t)_mm256_movemask_epi8(match_mask);
            return mask;
        }

        std::uint32_t matchUsed(const_pointer data) const {
            const __m256i kLowMask16 = _mm256_set1_epi16((short)0x00FF);
            __m256i tag_bits  = _mm256_set1_epi16(kEndOfMark);
            __m256i ctrl_bits = _mm256_loadu_si256((const __m256i *)data);
            __m256i low_bits  = _mm256_and_si256(ctrl_bits, kLowMask16);
            __m256i match_mask = _mm256_cmpgt_epi16(low_bits, tag_bits);
            std::uint32_t mask = (std::uint32_t)_mm256_movemask_epi8(match_mask);
            return mask;
        }

        std::uint32_t matchUnused(const_pointer data) const {
            return this->matchEmpty(data);
        }

        bool hasAnyMatch(const_pointer data, std::uint8_t control_hash) const {
            return (this->matchHash(data, control_hash) != 0);
        }

        bool hasAnyEmpty(const_pointer data) const {
            return (this->matchEmpty(data) != 0);
        }

        bool hasAnyUsed(const_pointer data) const {
            return (this->matchUsed(data) != 0);
        }

        bool hasAnyUnused(const_pointer data) const {
            return (this->matchUnused(data) != 0);
        }

        bool isAllEmpty(const_pointer data) const {
            return (this->matchEmpty(data) == kFullMask32);
        }

        bool isAllUsed(const_pointer data) const {
            return (this->matchUnused(data) == 0);
        }

        bool isAllUnused(const_pointer data) const {
            return (this->matchUnused(data) == kFullMask32);
        }
    };

    template <typename T>
    using BitMask256 = BitMask256_AVX2<T>;

#else

    static_assert(false, "jstd::robin32_hash_map<K,V> required Intel AVX2 intrinsics.")

#endif // __AVX2__

    struct map_group {
        typedef BitMask256<control_type>                        bitmask256_type;
        typedef typename BitMask256<control_type>::bitmask_type bitmask_type;

        union {
            control_type controls[kGroupWidth];
            bitmask256_type bitmask;
        };

        map_group() noexcept {
        }
        ~map_group() noexcept = default;

        void clear() {
            bitmask.clear(&this->controls[0]);
        }

        template <std::uint8_t ControlTag>
        void fillAll16() {
            bitmask.template fillAll16<ControlTag>(&this->controls[0]);
        }

        bitmask_type matchControlTag(std::uint8_t control_tag) const {
            return bitmask.matchControlTag(&this->controls[0], control_tag);
        }

        bitmask_type matchHash(std::uint8_t control_hash) const {
            return bitmask.matchHash(&this->controls[0], control_hash);
        }

        MatchMask2<bitmask_type>
        matchHashAndEmpty(std::uint8_t control_hash) const {
            return bitmask.matchHashAndEmpty(&this->controls[0], control_hash);
        }

        MatchMask3<bitmask_type>
        matchHashAndDistance(std::uint8_t ctrl_hash, std::uint8_t distance) const {
            return bitmask.matchHashAndDistance(&this->controls[0], ctrl_hash, distance);
        }

        bitmask_type matchEmpty() const {
            return bitmask.matchEmpty(&this->controls[0]);
        }

        bitmask_type matchEmptyOrZero() const {
            return bitmask.matchEmptyOrZero(&this->controls[0]);
        }

        bitmask_type matchUsed() const {
            return bitmask.matchUsed(&this->controls[0]);
        }

        bitmask_type matchUnused() const {
            return bitmask.matchUnused(&this->controls[0]);
        }

        bool hasAnyMatch(std::uint8_t control_hash) const {
            return bitmask.hasAnyMatch(&this->controls[0], control_hash);
        }

        bool hasAnyEmpty() const {
            return bitmask.hasAnyEmpty(&this->controls[0]);
        }

        bool hasAnyUsed() const {
            return bitmask.hasAnyUsed(&this->controls[0]);
        }

        bool hasAnyUnused() const {
            return bitmask.hasAnyUnused(&this->controls[0]);
        }

        bool isAllEmpty() const {
            return bitmask.isAllEmpty(&this->controls[0]);
        }

        bool isAllUsed() const {
            return bitmask.isAllUsed(&this->controls[0]);
        }

        bool isAllUnused() const {
            return bitmask.isAllUnused(&this->controls[0]);
        }
    };

    typedef map_group  group_type;

    class slot_type {
    public:
        union {
            value_type          value;
            mutable_value_type  mutable_value;
            const key_type      key;
            key_type            mutable_key;
        };

        slot_type() {}
        ~slot_type() = delete;
    };

    typedef slot_type       mutable_slot_type;
    typedef slot_type       node_type;

    template <typename ValueType>
    class basic_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;

        using value_type = ValueType;
        using pointer = ValueType *;
        using reference = ValueType &;

        using remove_const_value_type = typename std::remove_const<ValueType>::type;

        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;

    private:
        control_type * ctrl_;
        slot_type *    slot_;

    public:
        basic_iterator() noexcept : ctrl_(nullptr), slot_(nullptr) {
        }
        basic_iterator(control_type * ctrl, slot_type * slot) noexcept
            : ctrl_(ctrl), slot_(slot) {
        }
        basic_iterator(const basic_iterator & src) noexcept
            : ctrl_(src.ctrl_), slot_(src.slot_) {
        }

        basic_iterator & operator = (const basic_iterator & rhs) {
            this->ctrl_ = rhs.ctrl_;
            this->slot_ = rhs.slot_;
            return *this;
        }

        friend bool operator == (const basic_iterator & lhs, const basic_iterator & rhs) {
            return (lhs.slot_ == rhs.slot_);
        }

        friend bool operator != (const basic_iterator & lhs, const basic_iterator & rhs) {
            return (lhs.slot_ != rhs.slot_);
        }

        basic_iterator & operator ++ () {
            do {
                ++(this->ctrl_);
                ++(this->slot_);
            } while (ctrl_->isEmpty());
            return *this;
        }

        basic_iterator operator ++ (int) {
            basic_iterator copy(*this);
            ++*this;
            return copy;
        }

        reference operator * () const {
            return this->slot_->value;
        }

        pointer operator -> () const {
            return std::addressof(this->slot_->value);
        }

        operator basic_iterator<const remove_const_value_type>() const {
            return { this->ctrl_, this->slot_ };
        }

        slot_type * value() {
            return this->slot_;
        }

        const slot_type * value() const {
            return this->slot_;
        }
    };

    using iterator       = basic_iterator<value_type>;
    using const_iterator = basic_iterator<const value_type>;

    typedef typename std::allocator_traits<allocator_type>::template rebind_alloc<mutable_value_type>
                                        mutable_allocator_type;
    typedef typename std::allocator_traits<allocator_type>::template rebind_alloc<slot_type>
                                        slot_allocator_type;
    typedef typename std::allocator_traits<allocator_type>::template rebind_alloc<mutable_slot_type>
                                        mutable_slot_allocator_type;

private:
    group_type *    groups_;
    size_type       group_mask_;

    slot_type *     slots_;
    size_type       slot_size_;
    size_type       slot_mask_;

    size_type       slot_threshold_;
    std::uint32_t   n_mlf_;
    std::uint32_t   n_mlf_rev_;

    hasher          hasher_;
    key_equal       key_equal_;

    allocator_type              allocator_;
    mutable_allocator_type      mutable_allocator_;
    slot_allocator_type         slot_allocator_;
    mutable_slot_allocator_type mutable_slot_allocator_;

public:
    robin32_hash_map() : robin32_hash_map(kDefaultCapacity) {
    }

    explicit robin32_hash_map(size_type init_capacity,
                              const hasher & hash = hasher(),
                              const key_equal & equal = key_equal(),
                              const allocator_type & alloc = allocator_type()) :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(hash), key_equal_(equal),
        allocator_(alloc), mutable_allocator_(alloc),
        slot_allocator_(alloc), mutable_slot_allocator_(alloc) {
        this->create_group<true>(init_capacity);
    }

    explicit robin32_hash_map(const allocator_type & alloc)
        : robin32_hash_map(kDefaultCapacity, hasher(), key_equal(), alloc) {
    }

    template <typename InputIter>
    robin32_hash_map(InputIter first, InputIter last,
                     size_type init_capacity = kDefaultCapacity,
                     const hasher & hash = hasher(),
                     const key_equal & equal = key_equal(),
                     const allocator_type & alloc = allocator_type()) :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(hash), key_equal_(equal),
        allocator_(alloc), mutable_allocator_(alloc),
        slot_allocator_(alloc), mutable_slot_allocator_(alloc) {
        // Prepare enough space to ensure that no expansion is required during the insertion process.
        size_type input_size = distance(first, last);
        size_type reserve_capacity = (init_capacity >= input_size) ? init_capacity : input_size;
        this->reserve_for_insert(reserve_capacity);
        this->insert(first, last);
    }

    template <typename InputIter>
    robin32_hash_map(InputIter first, InputIter last,
                     size_type init_capacity,
                     const allocator_type & alloc)
        : robin32_hash_map(first, last, init_capacity, hasher(), key_equal(), alloc) {
    }

    template <typename InputIter>
    robin32_hash_map(InputIter first, InputIter last,
                     size_type init_capacity,
                     const hasher & hash,
                     const allocator_type & alloc)
        : robin32_hash_map(first, last, init_capacity, hash, key_equal(), alloc) {
    }

    robin32_hash_map(const robin32_hash_map & other)
        : robin32_hash_map(other, std::allocator_traits<allocator_type>::
                                  select_on_container_copy_construction(other.get_allocator())) {
    }

    robin32_hash_map(const robin32_hash_map & other, const Allocator & alloc) :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(hasher()), key_equal_(key_equal()),
        allocator_(alloc), mutable_allocator_(alloc),
        slot_allocator_(alloc), mutable_slot_allocator_(alloc) {
        // Prepare enough space to ensure that no expansion is required during the insertion process.
        size_type other_size = other.slot_size();
        this->reserve_for_insert(other_size);
        try {
            this->insert_unique(other.begin(), other.end());
        } catch (const std::bad_alloc & ex) {
            this->destroy<true>();
            throw std::bad_alloc();
        } catch (...) {
            this->destroy<true>();
            throw;
        }
    }

    robin32_hash_map(robin32_hash_map && other) noexcept :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(std::move(other.hash_function())),
        key_equal_(std::move(other.key_eq())),
        allocator_(std::move(other.get_allocator())),
        mutable_allocator_(std::move(other.get_mutable_allocator())),
        slot_allocator_(std::move(other.get_slot_allocator())),
        mutable_slot_allocator_(std::move(other.get_mutable_slot_allocator())) {
        // Swap content only
        this->swap_content(other);
    }

    robin32_hash_map(robin32_hash_map && other, const Allocator & alloc) noexcept :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(std::move(other.hash_function())),
        key_equal_(std::move(other.key_eq())),
        allocator_(alloc),
        mutable_allocator_(std::move(other.get_mutable_allocator())),
        slot_allocator_(std::move(other.get_slot_allocator())),
        mutable_slot_allocator_(std::move(other.get_mutable_slot_allocator())) {
        // Swap content only
        this->swap_content(other);
    }

    robin32_hash_map(std::initializer_list<value_type> init_list,
                     size_type init_capacity = kDefaultCapacity,
                     const hasher & hash = hasher(),
                     const key_equal & equal = key_equal(),
                     const allocator_type & alloc = allocator_type()) :
        groups_(nullptr), group_mask_(0),
        slots_(nullptr), slot_size_(0), slot_mask_(0),
        slot_threshold_(0), n_mlf_(kDefaultLoadFactorInt),
        n_mlf_rev_(kDefaultLoadFactorRevInt),
        hasher_(hash), key_equal_(equal),
        allocator_(alloc), mutable_allocator_(alloc),
        slot_allocator_(alloc), mutable_slot_allocator_(alloc) {
        // Prepare enough space to ensure that no expansion is required during the insertion process.
        size_type reserve_capacity = (init_capacity >= init_list.size()) ? init_capacity : init_list.size();
        this->reserve_for_insert(reserve_capacity);
        this->insert(init_list.begin(), init_list.end());
    }

    robin32_hash_map(std::initializer_list<value_type> init_list,
                     size_type init_capacity,
                     const allocator_type & alloc)
        : robin32_hash_map(init_list, init_capacity, hasher(), key_equal(), alloc) {
    }

    robin32_hash_map(std::initializer_list<value_type> init_list,
                     size_type init_capacity,
                     const hasher & hash,
                     const allocator_type & alloc)
        : robin32_hash_map(init_list, init_capacity, hash, key_equal(), alloc) {
    }

    ~robin32_hash_map() {
        this->destroy<true>();
    }

    bool is_valid() const { return (this->groups() != nullptr); }
    bool is_empty() const { return (this->size() == 0); }
    bool is_full() const  { return (this->size() > this->slot_mask()); }

    bool empty() const { return this->is_empty(); }

    size_type size() const { return slot_size_; }
    size_type capacity() const { return (slot_mask_ + 1); }

    group_type * groups() { return groups_; }
    const group_type * groups() const { return groups_; }

    control_type * controls() { return (control_type *)this->groups(); }
    const control_type * controls() const { return (const control_type *)this->groups(); }

    size_type group_mask() const { return group_mask_; }
    size_type group_count() const { return (group_mask_ + 1); }
    size_type group_capacity() const { return (this->group_count() + 1); }

    slot_type * slots() { return slots_; }
    const slot_type * slots() const { return slots_; }

    size_type slot_size() const { return slot_size_; }
    size_type slot_mask() const { return slot_mask_; }
    size_type slot_capacity() const { return (slot_mask_ + 1); }
    size_type slot_threshold() const { return slot_threshold_; }
    size_type slot_threshold(size_type now_slow_capacity) const {
        return (now_slow_capacity * this->integral_mlf() / kLoadFactorAmplify);
    }

    constexpr size_type bucket_count() const {
        return kGroupWidth;
    }

    constexpr size_type bucket(const key_type & key) const {
        size_type index = this->find_impl(key);
        return ((index != npos) ? (index / kGroupWidth) : npos);
    }

    float load_factor() const {
        return ((float)this->slot_size() / this->slot_capacity());
    }

    void max_load_factor(float mlf) {
        if (mlf < kMinLoadFactor)
            mlf = kMinLoadFactor;
        if (mlf > kMaxLoadFactor)
            mlf = kMaxLoadFactor;
        assert(mlf != 0.0f);
        
        std::uint32_t n_mlf = (std::uint32_t)std::ceil(mlf * kLoadFactorAmplify);
        std::uint32_t n_mlf_rev = (std::uint32_t)std::ceil(1.0f / mlf * kLoadFactorAmplify);
        this->n_mlf_ = n_mlf;
        this->n_mlf_rev_ = n_mlf_rev;
        size_type now_slot_size = this->slot_size();
        size_type new_slot_threshold = this->slot_threshold();
        if (now_slot_size > new_slot_threshold) {
            size_type min_required_capacity = this->min_require_capacity(now_slot_size);
            if (min_required_capacity > this->slot_capacity()) {
                this->rehash(now_slot_size);
            }
        }
    }

    std::uint32_t integral_mlf() const {
        return this->n_mlf_;
    }

    std::uint32_t integral_mlf_rev() const {
        return this->n_mlf_rev_;
    }

    float max_load_factor() const {
        return ((float)this->integral_mlf() / kLoadFactorAmplify);
    }

    float default_mlf() const {
        return kDefaultLoadFactor;
    }

    size_type mul_mlf(size_type capacity) const {
        return (capacity * this->integral_mlf() / kLoadFactorAmplify);
    }

    size_type div_mlf(size_type capacity) const {
        return (capacity * this->integral_mlf_rev() / kLoadFactorAmplify);
    }

    iterator begin() {
#if 1
        assert(!(this->control_at(this->slot_capacity())->isEmpty()));
        group_type * group = this->groups();
        group_type * last_group = this->groups() + this->group_count();
        size_type start_index = 0;
        for (; group != last_group; group++) {
            std::uint32_t maskUsed = group->matchUsed();
            if (maskUsed != 0) {
                size_type pos = BitUtils::bsf32(maskUsed);
                size_type index = start_index + pos;
                return this->iterator_at(index);
            }
            start_index += kGroupWidth;
        }

        return this->iterator_at(this->slot_capacity());
#else
        control_type * control = this->controls();
        size_type index;
        for (index = 0; index <= this->slot_mask(); index++) {
            if (control->isUsed()) {
                return { control, this->slot_at(index) };
            }
            control++;
        }
        return { control, this->slot_at(index) };
#endif
    }

    const_iterator begin() const {
        return const_cast<this_type *>(this)->begin();
    }

    const_iterator cbegin() const {
        return this->begin();
    }

    iterator end() {
        return iterator_at(this->slot_capacity());
    }

    const_iterator end() const {
        return iterator_at(this->slot_capacity());
    }

    const_iterator cend() const {
        return this->end();
    }

    allocator_type get_allocator() const noexcept {
        return this->allocator_;
    }

    mutable_allocator_type get_mutable_allocator() const noexcept {
        return this->mutable_allocator_;
    }

    slot_allocator_type get_slot_allocator() const noexcept {
        return this->slot_allocator_;
    }

    mutable_slot_allocator_type get_mutable_slot_allocator() const noexcept {
        return this->mutable_slot_allocator_;
    }

    hasher hash_function() const {
        return this->hasher_;
    }

    key_equal key_eq() const {
        return this->key_equal_;
    }

    static const char * name() {
        return "jstd::robin32_hash_map<K, V>";
    }

    void clear(bool need_destory = false) noexcept {
        if (this->slot_capacity() > kDefaultCapacity) {
            if (need_destory) {
                this->destroy<true>();
                this->create_group<false>(kDefaultCapacity);
                assert(this->slot_size() == 0);
                return;
            }
        }
        this->destroy<false>();
        assert(this->slot_size() == 0);
    }

    void reserve(size_type new_capacity, bool read_only = false) {
        this->rehash(new_capacity, read_only);
    }

    void resize(size_type new_capacity, bool read_only = false) {
        this->rehash(new_capacity, read_only);
    }

    void rehash(size_type new_capacity, bool read_only = false) {
        if (!read_only)
            new_capacity = (std::max)(this->min_require_capacity(new_capacity), this->slot_size());
        else
            new_capacity = (std::max)(new_capacity, this->slot_size());
        this->rehash_impl<true, false>(new_capacity);
    }

    void shrink_to_fit(bool read_only = false) {
        size_type new_capacity;
        if (!read_only)
            new_capacity = this->min_require_capacity(this->slot_size());
        else
            new_capacity = this->slot_size();
        this->rehash_impl<true, false>(new_capacity);
    }

    void swap(robin32_hash_map & other) {
        if (&other != this) {
            this->swap_impl(other);
        }
    }

    mapped_type & operator [] (const key_type & key) {
        return this->try_emplace(key).first->second;
    }

    mapped_type & operator [] (key_type && key) {
        return this->try_emplace(std::move(key)).first->second;
    }

    mapped_type & at(const key_type & key) {
        size_type index = this->find_impl(key);
        if (index != npos) {
            slot_type * slot = this->slot_at(index);
            return slot->second;
        } else {
            throw std::out_of_range("std::out_of_range exception: jstd::robin32_hash_map<K,V>::at(key), "
                                    "the specified key is not exists.");
        }
    }

    const mapped_type & at(const key_type & key) const {
        size_type index = this->find_impl(key);
        if (index != npos) {
            slot_type * slot = this->slot_at(index);
            return slot->second;
        } else {
            throw std::out_of_range("std::out_of_range exception: jstd::robin32_hash_map<K,V>::at(key) const, "
                                    "the specified key is not exists.");
        }
    }

    size_type count(const key_type & key) const {
        size_type index = this->find_impl(key);
        return (index != npos) ? size_type(1) : size_type(0);
    }

    bool contains(const key_type & key) const {
        size_type index = this->find_impl(key);
        return (index != npos);
    }

    iterator find(const key_type & key) {
        size_type index = this->find_impl(key);
        if (index != npos)
            return this->iterator_at(index);
        else
            return this->end();
    }

    const_iterator find(const key_type & key) const {
        return const_cast<this_type *>(this)->find(key);
    }

    std::pair<iterator, iterator> equal_range(const key_type & key) {
        iterator iter = this->find(key);
        if (iter != this->end())
            return { iter, std::next(iter) };
        else
            return { iter, iter };
    }

    std::pair<const_iterator, const_iterator> equal_range(const key_type & key) const {
        const_iterator iter = this->find(key);
        if (iter != this->end())
            return { iter, std::next(iter) };
        else
            return { iter, iter };
    }

    std::pair<iterator, bool> insert(const value_type & value) {
        return this->emplace_impl<false>(value);
    }

    std::pair<iterator, bool> insert(value_type && value) {
        return this->emplace_impl<false>(std::move(value));
    }

    template <typename P, typename std::enable_if<
              (!jstd::is_same_ex<P, value_type>::value) &&
              (!jstd::is_same_ex<P, mutable_value_type>::value) &&
              std::is_constructible<value_type, P &&>::value>::type * = nullptr>
    std::pair<iterator, bool> insert(P && value) {
        return this->emplace_impl<false>(std::forward<P>(value));
    }

    iterator insert(const_iterator hint, const value_type & value) {
        return this->emplace_impl<false>(value).first;
    }

    iterator insert(const_iterator hint, value_type && value) {
        return this->emplace_impl<false>(std::move(value)).first;
    }

    template <typename P, typename std::enable_if<
              (!jstd::is_same_ex<P, value_type>::value) &&
              (!jstd::is_same_ex<P, mutable_value_type>::value) &&
              std::is_constructible<value_type, P &&>::value>::type * = nullptr>
    std::pair<iterator, bool> insert(const_iterator hint, P && value) {
        return this->emplace_impl<false>(std::forward<P>(value));
    }

    template <typename InputIter>
    void insert(InputIter first, InputIter last) {
        for (; first != last; ++first) {
            this->emplace_impl<false>(*first);
        }
    }

    void insert(std::initializer_list<value_type> ilist) {
        this->insert(ilist.begin(), ilist.end());
    }

    template <typename KeyType, typename MappedType>
    std::pair<iterator, bool> insert_or_assign(const KeyType & key, MappedType && value) {
        return this->emplace_impl<true>(key, std::forward<MappedType>(value));
    }

    template <typename KeyType, typename MappedType>
    std::pair<iterator, bool> insert_or_assign(KeyType && key, MappedType && value) {
        return this->emplace_impl<true>(std::move(key), std::forward<MappedType>(value));
    }

    template <typename KeyType, typename MappedType>
    iterator insert_or_assign(const_iterator hint, const KeyType & key, MappedType && value) {
        return this->emplace_impl<true>(key, std::forward<MappedType>(value))->first;
    }

    template <typename KeyType, typename MappedType>
    iterator insert_or_assign(const_iterator hint, KeyType && key, MappedType && value) {
        return this->emplace_impl<true>(std::move(key), std::forward<MappedType>(value))->first;
    }

    std::pair<iterator, bool> insert_always(const value_type & value) {
        return this->emplace_impl<true>(value);
    }

    std::pair<iterator, bool> insert_always(value_type && value) {
        return this->emplace_impl<true>(std::move(value));
    }

    template <typename P, typename std::enable_if<
              (!jstd::is_same_ex<P, value_type>::value) &&
              (!jstd::is_same_ex<P, mutable_value_type>::value) &&
              std::is_constructible<value_type, P &&>::value>::type * = nullptr>
    std::pair<iterator, bool> insert_always(P && value) {
        return this->emplace_impl<true>(std::forward<P>(value));
    }

    template <typename ... Args>
    std::pair<iterator, bool> emplace(Args && ... args) {
        return this->emplace_impl<false>(std::forward<Args>(args)...);
    }

    template <typename ... Args>
    iterator emplace_hint(const_iterator hint, Args && ... args) {
        return this->emplace_impl<false>(std::forward<Args>(args)...).first;
    }

    template <typename ... Args>
    std::pair<iterator, bool> try_emplace(const key_type & key, Args && ... args) {
        return this->emplace_impl<false>(key, std::forward<Args>(args)...);
    }

    template <typename ... Args>
    std::pair<iterator, bool> try_emplace(key_type && key, Args && ... args) {
        return this->emplace_impl<false>(std::forward<key_type>(key), std::forward<Args>(args)...);
    }

    template <typename ... Args>
    std::pair<iterator, bool> try_emplace(const_iterator hint, const key_type & key, Args && ... args) {
        return this->emplace_impl<false>(key, std::forward<Args>(args)...);
    }

    template <typename ... Args>
    std::pair<iterator, bool> try_emplace(const_iterator hint, key_type && key, Args && ... args) {
        return this->emplace_impl<false>(std::forward<key_type>(key), std::forward<Args>(args)...);
    }

    size_type erase(const key_type & key) {
        size_type num_deleted = this->find_and_erase(key);
        return num_deleted;
    }

    iterator erase(iterator pos) {
        size_type index = this->index_of(pos);
        this->erase_slot(pos);
        return ++pos;
    }

    const_iterator erase(const_iterator pos) {
        size_type index = this->index_of(pos);
        this->erase_slot(index);
        return ++pos;
    }

    iterator erase(const_iterator first, const_iterator last) {
        for (; first != last; ++first) {
            size_type index = this->index_of(first);
            this->erase_slot(index);
        }
        return { first };
    }

private:
    JSTD_FORCED_INLINE
    size_type calc_capacity(size_type init_capacity) const noexcept {
        size_type new_capacity = (std::max)(init_capacity, kMinimumCapacity);
        if (!pow2::is_pow2(new_capacity)) {
            new_capacity = pow2::round_up<size_type, kMinimumCapacity>(new_capacity);
        }
        return new_capacity;
    }

    JSTD_FORCED_INLINE
    size_type min_require_capacity(size_type init_capacity) {
        size_type new_capacity = init_capacity * this->integral_mlf_rev() / kLoadFactorAmplify;
        return new_capacity;
    }

    bool is_positive(size_type value) const {
        return (std::intptr_t(value) >= 0);
    }

    iterator iterator_at(size_type index) noexcept {
        return { this->control_at(index), this->slot_at(index) };
    }

    const_iterator iterator_at(size_type index) const noexcept {
        return { this->control_at(index), this->slot_at(index) };
    }

    inline hash_code_t get_hash(const key_type & key) const noexcept {
        hash_code_t hash_code = static_cast<hash_code_t>(this->hasher_(key));
        return hash_code;
    }

    inline hash_code_t get_second_hash(hash_code_t value) const noexcept {
        hash_code_t hash_code;
        if (sizeof(size_type) == 4)
            hash_code = (hash_code_t)((size_type)value * 2654435761ul);
        else
            hash_code = (hash_code_t)((size_type)value * 14695981039346656037ull);
        return hash_code;
    }

    inline std::uint8_t get_ctrl_hash(hash_code_t hash_code) const noexcept {
        return static_cast<std::uint8_t>(hash_code & kControlHashMask);
    }

    size_type round_index(size_type index) const {
        return (index & this->slot_mask());
    }

    size_type round_index(size_type index, size_type slot_mask) const {
        assert(pow2::is_pow2(slot_mask + 1));
        return (index & slot_mask);
    }

    std::uint8_t round_distance(size_type distance) const {
        return ((distance <= kEndOfMark) ? std::uint8_t(distance) : kEndOfMark);
    }

    inline size_type index_salt() const noexcept {
        return (size_type)((std::uintptr_t)this->groups() >> 12);
    }

    inline size_type index_for(hash_code_t hash_code) const noexcept {
        if (kUseIndexSalt)
            return (((size_type)hash_code ^ this->index_salt()) & this->slot_mask());
        else
            return ((size_type)hash_code & this->slot_mask());
    }

    inline size_type index_for(hash_code_t hash_code, size_type slot_mask) const noexcept {
        assert(pow2::is_pow2(slot_mask + 1));
        if (kUseIndexSalt)
            return (((size_type)hash_code ^ this->index_salt()) & slot_mask);
        else
            return ((size_type)hash_code & slot_mask);
    }

    inline size_type prev_group(size_type group_index) const noexcept {
        return (size_type)(((size_type)group_index + this->group_mask()) & this->group_mask());
    }

    inline size_type next_group(size_type group_index) const noexcept {
        return (size_type)((size_type)(group_index + 1) & this->group_mask());
    }

    inline size_type slot_prev_group(size_type slot_index) const noexcept {
        return (size_type)(((size_type)slot_index + this->slot_mask()) & this->slot_mask());
    }

    inline size_type slot_next_group(size_type slot_index) const noexcept {
        return (size_type)((size_type)(slot_index + kGroupWidth) & this->slot_mask());
    }

    control_type * control_at(size_type slot_index) noexcept {
        assert(slot_index <= this->slot_capacity());
        return (this->controls() + slot_index);
    }

    const control_type * control_at(size_type slot_index) const noexcept {
        assert(slot_index <= this->slot_capacity());
        return (this->controls() + slot_index);
    }

    control_type * control_at_ex(size_type slot_index) noexcept {
        assert(slot_index <= (this->slot_capacity() + kGroupWidth));
        return (this->controls() + slot_index);
    }

    const control_type * control_at_ex(size_type slot_index) const noexcept {
        assert(slot_index <= (this->slot_capacity() + kGroupWidth));
        return (this->controls() + slot_index);
    }

    group_type * group_at(size_type slot_index) noexcept {
        assert(slot_index < this->slot_capacity());
        return (group_type *)(this->control_at(slot_index));
    }

    const group_type * group_at(size_type slot_index) const noexcept {
        assert(slot_index < this->slot_capacity());
        return (const group_type *)(this->control_at(slot_index));
    }

    group_type * physical_group_at(size_type group_index) noexcept {
        assert(group_index < this->group_count());
        return (this->groups() + group_index);
    }

    const group_type * physical_group_at(size_type group_index) const noexcept {
        assert(group_index < this->group_count());
        return (this->groups() + group_index);
    }

    slot_type * slot_at(size_type slot_index) noexcept {
        assert(slot_index <= this->slot_capacity());
        return (this->slots() + slot_index);
    }

    const slot_type * slot_at(size_type slot_index) const noexcept {
        assert(slot_index <= this->slot_capacity());
        return (this->slots() + slot_index);
    }

    control_type & get_control(size_type slot_index) {
        assert(slot_index < this->slot_capacity());
        control_type * controls = this->controls();
        return controls[slot_index];
    }

    const control_type & get_control(size_type slot_index) const {
        assert(slot_index < this->slot_capacity());
        control_type * controls = this->controls();
        return controls[slot_index];
    }

    group_type & get_group(size_type slot_index) {
        assert(slot_index < this->slot_capacity());
        return *this->group_at(slot_index);
    }

    const group_type & get_group(size_type slot_index) const {
        assert(slot_index < this->slot_capacity());
        return *this->group_at(slot_index);
    }

    group_type & get_physical_group(size_type group_index) {
        assert(group_index < this->group_count());
        return this->groups_[group_index];
    }

    const group_type & get_physical_group(size_type group_index) const {
        assert(group_index < this->group_count());
        return this->groups_[group_index];
    }

    slot_type & get_slot(size_type slot_index) {
        assert(slot_index < this->slot_capacity());
        return this->slots_[slot_index];
    }

    const slot_type & get_slot(size_type slot_index) const {
        assert(slot_index < this->slot_capacity());
        return this->slots_[slot_index];
    }

    size_type index_of(iterator pos) const {
        return this->index_of(pos.value());
    }

    size_type index_of(const_iterator pos) const {
        return this->index_of(pos.value());
    }

    size_type index_of(slot_type * slot) const {
        assert(slot != nullptr);
        assert(slot >= this->slots());
        size_type index = (size_type)(slot - this->slots());
        assert(is_positive(index));
        return index;
    }

    size_type index_of(const slot_type * slot) const {
        return this->index_of((slot_type *)slot);
    }

    static void placement_new_slot(slot_type * slot) {
        // The construction of union doesn't do anything at runtime but it allows us
        // to access its members without violating aliasing rules.
        new (slot) slot_type;
    }

    template <bool finitial>
    void destroy() noexcept {
        this->destory_slots<finitial>();

        // Note!!: destory_slots() need use this->groups()
        this->destory_group<finitial>();
    }

    template <bool finitial>
    void destory_group() noexcept {
        if (this->groups_ != nullptr) {
            if (!finitial) {
                for (size_type group_index = 0; group_index <= this->group_mask(); group_index++) {
                    group_type * group = this->group_at(group_index);
                    group->clear();
                }
            }
            if (finitial) {
                delete[] this->groups_;
                this->groups_ = nullptr;
            }
        }
    }

    template <bool finitial>
    void destory_slots() noexcept(is_slot_trivial_destructor) {
        // Destroy all slots.
        if (this->slots_ != nullptr) {
            if (!is_slot_trivial_destructor) {
                control_type * control = this->controls();
                assert(control != nullptr);
                for (size_type index = 0; index <= this->slot_mask(); index++) {
                    if (control->isUsed()) {
                        this->destroy_mutable_slot(index);
                    }
                    control++;
                }
            }
            if (finitial) {
                this->slot_allocator_.deallocate(this->slots_, this->slot_capacity());
                this->slots_ = nullptr;
            }
        }

        this->slot_size_ = 0;
    }

    JSTD_FORCED_INLINE
    void destroy_slot(size_type index) {
        slot_type * slot = this->slot_at(index);
        this->destroy_slot(slot);
    }

    JSTD_FORCED_INLINE
    void destroy_slot(slot_type * slot) {
        this->allocator_.destroy(&slot->value);
    }

    JSTD_FORCED_INLINE
    void destroy_mutable_slot(size_type index) {
        slot_type * slot = this->slot_at(index);
        this->destroy_mutable_slot(slot);
    }

    JSTD_FORCED_INLINE
    void destroy_mutable_slot(slot_type * slot) {
        if (kIsCompatibleLayout) {
            this->mutable_allocator_.destroy(&slot->mutable_value);
        } else {
            this->allocator_.destroy(&slot->value);
        }
    }

    template <bool WriteEndofMark>
    JSTD_FORCED_INLINE
    void copy_and_mirror_controls() {
        // Copy and mirror the beginning control bytes.
        size_type copy_size = (std::min)(this->slot_capacity(), kGroupWidth);
        control_type * controls = this->controls();
        std::memcpy((void *)&controls[this->slot_capacity()], (const void *)&controls[0],
                    sizeof(control_type) * copy_size);

        if (WriteEndofMark) {
            // Set the end of mark
            control_type * ctrl_0 = controls;
            if (ctrl_0->isEmpty()) {
                control_type * ctrl_mirror = this->control_at(this->slot_capacity());
                ctrl_mirror->setEndOf();
            }
        }
    }

#if 0
    JSTD_FORCED_INLINE
    void setMirrorCtrl(size_type index, std::uint8_t tag, std::uint8_t distance) {
        if (index < kGroupWidth) {
            control_byte * ctrl_mirror = this->control_at_ex(index + this->slot_capacity());
            ctrl_mirror->setHash(ctrl_hash);
            ctrl_mirror->setDistance(distance);
            if (index == 0) {
                if (control_byte::isEmpty(tag)) {
                    ctrl_mirror->setEndOf();
                }
            }
        }
    }
#endif

    JSTD_FORCED_INLINE
    void setUsedMirrorCtrl(size_type index, std::uint8_t ctrl_hash, std::uint8_t distance) {
        assert(control_type::isUsed(distance));
        if (index < kGroupWidth) {
            control_type * ctrl_mirror = this->control_at_ex(index + this->slot_capacity());
            ctrl_mirror->setHash(ctrl_hash);
            ctrl_mirror->setDistance(distance);
        }
    }

    JSTD_FORCED_INLINE
    void setUnusedMirrorCtrl(size_type index, std::uint8_t tag) {
        if (index < kGroupWidth) {
            control_type * ctrl_mirror = this->control_at_ex(index + this->slot_capacity());
            ctrl_mirror->setDistance(tag);
            if (index == 0) {
                assert(control_type::isEmpty(tag));
                ctrl_mirror->setEndOf();
            }
        }
    }

    JSTD_FORCED_INLINE
    void setUsedCtrl(size_type index, std::uint8_t ctrl_hash, std::uint8_t distance) {        
        control_type * ctrl = this->control_at(index);
        ctrl->setUsed(ctrl_hash, distance);
        this->setUsedMirrorCtrl(index, ctrl_hash, distance);
    }

    JSTD_FORCED_INLINE
    void setUnusedCtrl(size_type index, std::uint8_t tag) {
        control_type * ctrl = this->control_at(index);
        assert(ctrl->isUsed());
        ctrl->setDistance(tag);
        this->setUnusedMirrorCtrl(index, tag);
    }

    inline bool need_grow() const {
        return (this->slot_threshold_ <= 0);
    }

    void grow_if_necessary() {
        size_type new_capacity = (this->slot_mask_ + 1) * 2;
        this->rehash_impl<false, true>(new_capacity);
    }

    JSTD_FORCED_INLINE
    void reserve_for_insert(size_type init_capacity) {
        size_type new_capacity = this->min_require_capacity(init_capacity);
        this->create_group<true>(new_capacity);
    }

    template <bool initialize = false>
    void create_group(size_type init_capacity) {
        size_type new_capacity;
        if (initialize)
            new_capacity = calc_capacity(init_capacity);
        else
            new_capacity = init_capacity;
        assert(new_capacity > 0);
        assert(new_capacity >= kMinimumCapacity);

        size_type group_count = (new_capacity + (kGroupWidth - 1)) / kGroupWidth;
        assert(group_count > 0);
        group_type * new_groups = new group_type[group_count + 1];
        groups_ = new_groups;
        group_mask_ = group_count - 1;

        for (size_type index = 0; index < group_count; index++) {
            new_groups[index].template fillAll16<kEmptyEntry>();
        }
        if (new_capacity >= kGroupWidth) {
            new_groups[group_count].template fillAll16<kEmptyEntry>();
        } else {
            assert(new_capacity < kGroupWidth);
            group_type * tail_group = (group_type *)((char *)new_groups + new_capacity * 2);
            (*tail_group).template fillAll16<kEndOfMark>();

            new_groups[group_count].template fillAll16<kEndOfMark>();
        }

        control_type * new_controls = (control_type *)new_groups;
        control_type * endof_ctrl = new_controls + new_capacity;
        endof_ctrl->setEndOf();

        slot_type * slots = slot_allocator_.allocate(new_capacity);
        slots_ = slots;
        if (initialize) {
            assert(slot_size_ == 0);
        } else {
            slot_size_ = 0;
        }
        slot_mask_ = new_capacity - 1;
        slot_threshold_ = this->slot_threshold(new_capacity);
    }

    template <bool AllowShrink, bool AlwaysResize>
    void rehash_impl(size_type new_capacity) {
        new_capacity = this->calc_capacity(new_capacity);
        assert(new_capacity > 0);
        assert(new_capacity >= kMinimumCapacity);
        if (AlwaysResize ||
            (!AllowShrink && (new_capacity > this->slot_capacity())) ||
            (AllowShrink && (new_capacity != this->slot_capacity()))) {
            if (!AlwaysResize && !AllowShrink) {
                assert(new_capacity >= this->slot_size());
            }

            group_type * old_groups = this->groups();
            control_type * old_controls = this->controls();
            size_type old_group_count = this->group_count();
            size_type old_group_capacity = this->group_capacity();

            slot_type * old_slots = this->slots();
            size_type old_slot_size = this->slot_size();
            size_type old_slot_mask = this->slot_mask();
            size_type old_slot_capacity = this->slot_capacity();

            this->create_group<false>(new_capacity);

            if ((this->max_load_factor() < 0.5f) && false) {
                control_type * last_control = old_controls + old_slot_capacity;
                slot_type * old_slot = old_slots;
                for (control_type * control = old_controls; control != last_control; control++) {
                    if (likely(control->isUsed())) {
                        this->move_insert_unique(old_slot);
                        if (!is_slot_trivial_destructor) {
                            this->destroy_mutable_slot(old_slot);
                        }
                    }
                    old_slot++;
                }
            } else {
                if (old_slot_capacity >= kGroupWidth) {
#if 1
                    group_type * last_group = old_groups + old_group_count;
                    size_type start_index = 0;
                    for (group_type * group = old_groups; group != last_group; group++) {
                        std::uint32_t maskUsed = group->matchUsed();
                        while (maskUsed != 0) {
                            size_type pos = BitUtils::bsf32(maskUsed);
                            maskUsed = BitUtils::clearLowBit32(maskUsed);
                            size_type old_index = start_index + pos;
                            slot_type * old_slot = old_slots + old_index;
                            this->move_insert_unique(old_slot);
                            if (!is_slot_trivial_destructor) {
                                this->destroy_mutable_slot(old_slot);
                            }
                        }
                        start_index += kGroupWidth;
                    }
#else
                    group_type * last_group = old_groups + old_group_count - 1;
                    size_type start_index = old_slot_capacity - kGroupWidth;
                    for (group_type * group = last_group; group >= old_groups; group--) {
                        std::uint32_t maskUsed = group->matchUsed();
                        while (maskUsed != 0) {
                            size_type pos = BitUtils::bsf32(maskUsed);
                            maskUsed = BitUtils::clearLowBit32(maskUsed);
                            size_type old_index = start_index + pos;
                            slot_type * old_slot = old_slots + old_index;
                            this->move_insert_unique(old_slot);
                            if (!is_slot_trivial_destructor) {
                                this->destroy_mutable_slot(old_slot);
                            }
                        }
                        start_index -= kGroupWidth;
                    }
#endif
                } else {
                    control_type * last_control = old_controls + old_slot_capacity;
                    slot_type * old_slot = old_slots;
                    for (control_type * control = old_controls; control != last_control; control++) {
                        if (likely(control->isUsed())) {
                            this->move_insert_unique(old_slot);
                            if (!is_slot_trivial_destructor) {
                                this->destroy_mutable_slot(old_slot);
                            }
                        }
                        old_slot++;
                    }
                }
            }

            assert(this->slot_size() == old_slot_size);
            slot_threshold_ -= this->slot_size();

            this->slot_allocator_.deallocate(old_slots, old_slot_capacity);

            if (old_groups != nullptr) {
                delete[] old_groups;
            }
        }
    }

    JSTD_FORCED_INLINE
    void transfer_slot(size_type dest_index, size_type src_index) {
        slot_type * dest_slot = this->slot_at(dest_index);
        slot_type * src_slot  = this->slot_at(src_index);
        this->transfer_slot(dest_slot, src_slot);
    }

    JSTD_FORCED_INLINE
    void transfer_slot(slot_type * dest_slot, slot_type * src_slot) {
        if (kIsCompatibleLayout) {
            this->mutable_allocator_.construct(&dest_slot->mutable_value,
                    std::move(*reinterpret_cast<mutable_value_type *>(&src_slot->mutable_value)));
            if (!is_slot_trivial_destructor) {
                this->destroy_mutable_slot(src_slot);
            }
        } else {
            this->allocator_.construct(&dest_slot->value, std::move(*reinterpret_cast<value_type *>(&src_slot->value)));
            if (!is_slot_trivial_destructor) {
                this->destroy_slot(src_slot);
            }
        }
    }

    JSTD_FORCED_INLINE
    void swap_slot(slot_type * slot1, slot_type * slot2, slot_type * tmp) {
#if 1
        using std::swap;
        if (kIsCompatibleLayout) {
            swap(*reinterpret_cast<mutable_value_type *>(&slot1->mutable_value),
                 *reinterpret_cast<mutable_value_type *>(&slot2->mutable_value));
        } else {
            swap(*reinterpret_cast<value_type *>(&slot1->value),
                 *reinterpret_cast<value_type *>(&slot2->value));
        }
#elif 1
        /*
        if (kIsCompatibleLayout) {
            this->mutable_slot_allocator_.construct(tmp, std::move(*reinterpret_cast<mutable_slot_type *>(slot1)));
            this->mutable_slot_allocator_.construct(slot1, std::move(*reinterpret_cast<mutable_slot_type *>(slot2)));
            this->mutable_slot_allocator_.construct(slot2, std::move(*reinterpret_cast<mutable_slot_type *>(tmp)));
            if (!is_slot_trivial_destructor) {
                this->mutable_slot_allocator_.destroy(tmp);
            }
        } else {
            //
        }
        //*/
#else
        /*
        if (kIsCompatibleLayout) {
            this->mutable_slot_allocator_.construct(tmp, std::move(*reinterpret_cast<mutable_slot_type *>(slot1)));
            *slot1 = std::move(*reinterpret_cast<mutable_slot_type *>(slot2));
            *slot2 = std::move(*reinterpret_cast<mutable_slot_type *>(tmp));
            if (!is_slot_trivial_destructor) {
                this->mutable_slot_allocator_.destroy(tmp);
            }
        } else {
            //
        }
        //*/
#endif
    }

    size_type find_impl(const key_type & key) const {
        hash_code_t hash_code = this->get_hash(key);
        hash_code_t hash_code_2nd = this->get_second_hash(hash_code);
        std::uint8_t ctrl_hash = this->get_ctrl_hash(hash_code_2nd);
        size_type slot_index = this->index_for(hash_code);
        size_type start_slot = slot_index;
        std::uint8_t distance = 0;
        do {
            const group_type & group = this->get_group(slot_index);
            auto mask32 = group.matchHashAndDistance(ctrl_hash, distance);
            std::uint32_t maskHash = mask32.maskHash;
            size_type start_index = slot_index;
            while (maskHash != 0) {
                size_type pos = BitUtils::bsf32(maskHash);
                maskHash = BitUtils::clearLowBit32(maskHash);
                size_type index = this->round_index(start_index + (pos >> 1));
                const slot_type & target = this->get_slot(index);
                if (this->key_equal_(target.value.first, key)) {
                    return index;
                }
            }
            if (mask32.maskEmpty != 0) {
                return npos;
            }
            slot_index = this->slot_next_group(slot_index);
        } while (slot_index != start_slot);

        return npos;
    }

    JSTD_FORCED_INLINE
    size_type find_impl(const key_type & key, size_type & first_slot,
                        size_type & last_slot, std::uint8_t & o_ctrl_hash) const {
        hash_code_t hash_code = this->get_hash(key);
        hash_code_t hash_code_2nd = this->get_second_hash(hash_code);
        std::uint8_t ctrl_hash = this->get_ctrl_hash(hash_code_2nd);
        size_type slot_index = this->index_for(hash_code);
        size_type start_slot = slot_index;
        std::uint8_t distance = 0;
        first_slot = start_slot;
        o_ctrl_hash = ctrl_hash;
        do {
            const group_type & group = this->get_group(slot_index);
            auto mask32 = group.matchHashAndDistance(ctrl_hash, distance);
            std::uint32_t maskHash = mask32.maskHash;
            size_type start_index = slot_index;
            while (maskHash != 0) {
                size_type pos = BitUtils::bsf32(maskHash);
                maskHash = BitUtils::clearLowBit32(maskHash);
                size_type index = this->round_index(start_index + (pos >> 1));
                const slot_type & target = this->get_slot(index);
                if (this->key_equal_(target.value.first, key)) {
                    last_slot = slot_index;
                    return index;
                }
            }
            if (mask32.maskEmpty != 0) {
                last_slot = slot_index;
                return npos;
            }
            distance += kGroupWidth;
            slot_index = this->slot_next_group(slot_index);
        } while (slot_index != start_slot);

        last_slot = npos;
        return npos;
    }

    JSTD_FORCED_INLINE
    size_type find_first_empty_slot(const key_type & key, std::uint8_t & o_ctrl_hash,
                                                          std::uint8_t & distance) {
        hash_code_t hash_code = this->get_hash(key);
        hash_code_t hash_code_2nd = this->get_second_hash(hash_code);
        std::uint8_t ctrl_hash = this->get_ctrl_hash(hash_code_2nd);
        size_type slot_index = this->index_for(hash_code);
        size_type first_slot = slot_index;
        o_ctrl_hash = ctrl_hash;

        // Find the first empty slot and insert
        do {
            const group_type & group = this->get_group(slot_index);
            std::uint32_t maskEmpty = group.matchEmpty();
            if (maskEmpty != 0) {
                // Found a [EmptyEntry] to insert
                size_type pos = BitUtils::bsf32(maskEmpty);
                size_type index = slot_index + (pos >> 1);
                distance = this->round_distance(index - first_slot);
                return this->round_index(index);
            }
            slot_index = this->slot_next_group(slot_index);
            assert(slot_index != first_slot);
        } while (1);

        distance = kEmptyEntry;
        return npos;
    }

    // Use in rehash_impl()
    void move_insert_unique(slot_type * slot) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        size_type target = this->find_first_empty_slot(slot->value.first,
                                                       ctrl_hash, distance);
        assert(target != npos);

        // Found a [EmptyEntry] to insert
        assert(this->control_at(target)->isEmpty());
        this->setUsedCtrl(target, ctrl_hash, distance);

        slot_type * new_slot = this->slot_at(target);
        if (kIsCompatibleLayout) {
            this->mutable_allocator_.construct(&new_slot->mutable_value,
                                               std::move(*static_cast<mutable_value_type *>(&slot->mutable_value)));
        } else {
            this->allocator_.construct(&new_slot->value, std::move(slot->value));
        }
        this->slot_size_++;
        assert(this->slot_size() <= this->slot_capacity());
    }

    void insert_unique(const value_type & value) {
        std::uint8_t ctrl_hash;
        size_type target = this->find_first_empty_slot(value.first, ctrl_hash);
        assert(target != npos);

        // Found a [EmptyEntry] to insert
        assert(this->control_at(target)->isEmpty());
        this->setUsedCtrl(target, ctrl_hash, distance);

        slot_type * slot = this->slot_at(target);
        if (kIsCompatibleLayout) {
            this->mutable_allocator_.construct(&slot->mutable_value, value);
        } else {
            this->allocator_.construct(&slot->value, value);
        }
        this->slot_size_++;
        assert(this->slot_size() <= this->slot_capacity());
    }

    void insert_unique(value_type && value) {
        std::uint8_t ctrl_hash;
        size_type target = this->find_first_empty_slot(value.first, ctrl_hash);
        assert(target != npos);

        // Found a [EmptyEntry] to insert
        assert(this->control_at(target)->isEmpty());
        this->setUsedCtrl(target, ctrl_hash, distance);

        slot_type * slot = this->slot_at(target);
        if (kIsCompatibleLayout) {
            this->mutable_allocator_.construct(&slot->mutable_value,
                                               std::move(*static_cast<mutable_value_type *>(&value)));
        } else {
            this->allocator_.construct(&slot->value, std::move(value));
        }
        this->slot_size_++;
        assert(this->slot_size() <= this->slot_capacity());
    }

    // Use in constructor
    template <typename InputIter>
    void insert_unique(InputIter first, InputIter last) {
        for (InputIter iter = first; iter != last; ++iter) {
            this->insert_unique(static_cast<value_type>(*iter));
        }
    }

    std::pair<size_type, bool>
    find_and_prepare_insert(const key_type & key, std::uint8_t & o_ctrl_hash, std::uint8_t & o_distance) {
        hash_code_t hash_code = this->get_hash(key);
        hash_code_t hash_code_2nd = this->get_second_hash(hash_code);
        std::uint8_t ctrl_hash = this->get_ctrl_hash(hash_code_2nd);
        size_type slot_index = this->index_for(hash_code);
        size_type first_slot = slot_index;
        size_type last_slot = npos;
        std::uint8_t distance = 0;
        std::uint32_t maskEmpty;
        o_ctrl_hash = ctrl_hash;

        do {
            const group_type & group = this->get_group(slot_index);
            auto mask32 = group.matchHashAndDistance(ctrl_hash, distance);
            std::uint32_t maskHash = mask32.maskHash;
            while (maskHash != 0) {
                size_type pos = BitUtils::bsf32(maskHash);
                maskHash = BitUtils::clearLowBit32(maskHash);
                size_type index = slot_index + (pos >> 1);
                distance = this->round_distance(index);
                index = this->round_index(index);
                const slot_type & target = this->get_slot(index);
                if (this->key_equal_(target.value.first, key)) {
                    o_distance = distance;
                    return { index, true };
                }
            }
            maskEmpty = mask32.maskEmpty;
            if (maskEmpty != 0) {
                last_slot = slot_index;
                break;
            }
            distance += kGroupWidth;
            slot_index = this->slot_next_group(slot_index);
        } while (slot_index != first_slot);

        if (this->need_grow() || (last_slot == npos)) {
            // The size of slot reach the slot threshold or hashmap is full.
            this->grow_if_necessary();

            return this->find_and_prepare_insert(key, o_ctrl_hash, o_distance);
        }

        // It's a [EmptyEntry] to insert
        assert(maskEmpty != 0);
        size_type pos = BitUtils::bsf32(maskEmpty);
        size_type index = last_slot + (pos >> 1);
        o_distance = this->round_distance(index);
        index = this->round_index(index);
        return { index, false };
    }

    template <bool AlwaysUpdate>
    std::pair<iterator, bool> emplace_impl(const value_type & value) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        auto find_info = this->find_and_prepare_insert(value.first, ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            this->slot_allocator_.construct(slot, value);
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            if (AlwaysUpdate) {
                slot_type * slot = this->slot_at(target);
                slot->second = value.second;
            }
            return { this->iterator_at(target), false };
        }
    }

    template <bool AlwaysUpdate>
    std::pair<iterator, bool> emplace_impl(value_type && value) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        auto find_info = this->find_and_prepare_insert(value.first, ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found a [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            if (kIsCompatibleLayout)
                this->mutable_allocator_.construct(&slot->mutable_value, std::forward<value_type>(value));
            else
                this->allocator_.construct(&slot->value, std::forward<value_type>(value));
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            if (AlwaysUpdate) {
                static constexpr bool is_rvalue_ref = std::is_rvalue_reference<decltype(value)>::value;
                slot_type * slot = this->slot_at(target);
                if (is_rvalue_ref)
                    slot->value.second = std::move(value.second);
                else
                    slot->value.second = value.second;
            }
            return { this->iterator_at(target), false };
        }
    }

    template <bool AlwaysUpdate, typename KeyT, typename MappedT, typename std::enable_if<
              (!jstd::is_same_ex<KeyT, value_type>::value &&
               !std::is_constructible<value_type, KeyT &&>::value) &&
              (!jstd::is_same_ex<KeyT, mutable_value_type>::value &&
               !std::is_constructible<mutable_value_type, KeyT &&>::value) &&
              (!jstd::is_same_ex<KeyT, std::piecewise_construct_t>::value) &&
              (jstd::is_same_ex<KeyT, key_type>::value ||
               std::is_constructible<key_type, KeyT &&>::value) &&
              (jstd::is_same_ex<MappedT, mapped_type>::value ||
               std::is_constructible<mapped_type, MappedT &&>::value)>::type * = nullptr>
    std::pair<iterator, bool> emplace_impl(KeyT && key, MappedT && value) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        auto find_info = this->find_and_prepare_insert(key, ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found a [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            if (kIsCompatibleLayout) {
                this->mutable_allocator_.construct(&slot->mutable_value,
                                                   std::forward<KeyT>(key),
                                                   std::forward<MappedT>(value));
            } else {
                this->allocator_.construct(&slot->value, std::forward<KeyT>(key),
                                                         std::forward<MappedT>(value));
            }
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            static constexpr bool isMappedType = jstd::is_same_ex<MappedT, mapped_type>::value;
            if (AlwaysUpdate) {
                if (isMappedType) {
                    slot_type * slot = this->slot_at(target);
                    slot->value.second = std::forward<MappedT>(value);
                } else {
                    mapped_type mapped_value(std::forward<MappedT>(value));
                    slot_type * slot = this->slot_at(target);
                    slot->value.second = std::move(mapped_value);
                }
            }
            return { this->iterator_at(target), false };
        }
    }

    template <bool AlwaysUpdate, typename KeyT, typename std::enable_if<
              (!jstd::is_same_ex<KeyT, value_type>::value &&
               !std::is_constructible<value_type, KeyT &&>::value) &&
              (!jstd::is_same_ex<KeyT, mutable_value_type>::value &&
               !std::is_constructible<mutable_value_type, KeyT &&>::value) &&
              (!jstd::is_same_ex<KeyT, std::piecewise_construct_t>::value) &&
              (jstd::is_same_ex<KeyT, key_type>::value ||
               std::is_constructible<key_type, KeyT &&>::value)>::type * = nullptr,
              typename ... Args>
    std::pair<iterator, bool> emplace_impl(KeyT && key, Args && ... args) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        auto find_info = this->find_and_prepare_insert(key, ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found a [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            if (kIsCompatibleLayout) {
                this->mutable_allocator_.construct(&slot->mutable_value,
                                                   std::piecewise_construct,
                                                   std::forward_as_tuple(std::forward<KeyT>(key)),
                                                   std::forward_as_tuple(std::forward<Args>(args)...));
            } else {
                this->allocator_.construct(&slot->value,
                                           std::piecewise_construct,
                                           std::forward_as_tuple(std::forward<KeyT>(key)),
                                           std::forward_as_tuple(std::forward<Args>(args)...));
            }
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            if (AlwaysUpdate) {
                mapped_type mapped_value(std::forward<Args>(args)...);
                slot_type * slot = this->slot_at(target);
                slot->value.second = std::move(mapped_value);
            }
            return { this->iterator_at(target), false };
        }
    }

    template <bool AlwaysUpdate, typename PieceWise, typename std::enable_if<
              (!jstd::is_same_ex<PieceWise, value_type>::value &&
               !std::is_constructible<value_type, PieceWise &&>::value) &&
              (!jstd::is_same_ex<PieceWise, mutable_value_type>::value &&
               !std::is_constructible<mutable_value_type, PieceWise &&>::value) &&
              jstd::is_same_ex<PieceWise, std::piecewise_construct_t>::value &&
              (!jstd::is_same_ex<PieceWise, key_type>::value &&
               !std::is_constructible<key_type, PieceWise &&>::value)>::type * = nullptr,
              typename ... Ts1, typename ... Ts2>
    std::pair<iterator, bool> emplace_impl(PieceWise && hint,
                                           std::tuple<Ts1...> && first,
                                           std::tuple<Ts2...> && second) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        tuple_wrapper2<key_type> key_wrapper(first);
        auto find_info = this->find_and_prepare_insert(key_wrapper.value(), ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found a [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            if (kIsCompatibleLayout) {
                this->mutable_allocator_.construct(&slot->mutable_value,
                                                   std::piecewise_construct,
                                                   std::forward<std::tuple<Ts1...>>(first),
                                                   std::forward<std::tuple<Ts2...>>(second));
            } else {
                this->allocator_.construct(&slot->value,
                                           std::piecewise_construct,
                                           std::forward<std::tuple<Ts1...>>(first),
                                           std::forward<std::tuple<Ts2...>>(second));
            }
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            if (AlwaysUpdate) {
                tuple_wrapper2<mapped_type> mapped_wrapper(std::move(second));
                slot_type * slot = this->slot_at(target);
                slot->value.second = std::move(mapped_wrapper.value());
            }
            return { this->iterator_at(target), false };
        }
    }

    template <bool AlwaysUpdate, typename First, typename std::enable_if<
              (!jstd::is_same_ex<First, value_type>::value &&
               !std::is_constructible<value_type, First &&>::value) &&
              (!jstd::is_same_ex<First, mutable_value_type>::value &&
               !std::is_constructible<mutable_value_type, First &&>::value) &&
              (!jstd::is_same_ex<First, std::piecewise_construct_t>::value) &&
              (!jstd::is_same_ex<First, key_type>::value &&
               !std::is_constructible<key_type, First &&>::value)>::type * = nullptr,
              typename ... Args>
    std::pair<iterator, bool> emplace_impl(First && first, Args && ... args) {
        std::uint8_t distance;
        std::uint8_t ctrl_hash;
        value_type value(std::forward<First>(first), std::forward<Args>(args)...);
        auto find_info = this->find_and_prepare_insert(value.first, ctrl_hash, distance);
        size_type target = find_info.first;
        bool is_exists = find_info.second;
        if (!is_exists) {
            // The key to be inserted is not exists.
            assert(target != npos);

            // Found a [EmptyEntry] to insert
            assert(this->control_at(target)->isEmpty());
            this->setUsedCtrl(target, ctrl_hash, distance);

            slot_type * slot = this->slot_at(target);
            if (kIsCompatibleLayout) {
                this->mutable_allocator_.construct(&slot->mutable_value, std::move(value));
            } else {
                this->allocator_.construct(&slot->value, std::move(value));
            }
            this->slot_size_++;
            return { this->iterator_at(target), true };
        } else {
            // The key to be inserted already exists.
            if (AlwaysUpdate) {
                slot_type * slot = this->slot_at(target);
                slot->value.second = std::move(value.second);
            }
            return { this->iterator_at(target), false };
        }
    }

    JSTD_FORCED_INLINE
    size_type find_and_erase(const key_type & key) {
        hash_code_t hash_code = this->get_hash(key);
        hash_code_t hash_code_2nd = this->get_second_hash(hash_code);
        std::uint8_t ctrl_hash = this->get_ctrl_hash(hash_code_2nd);
        size_type slot_index = this->index_for(hash_code);
        size_type start_slot = slot_index;
        std::uint8_t distance = 0;
        do {
            const group_type & group = this->get_group(slot_index);
            auto mask32 = group.matchHashAndDistance(ctrl_hash, distance);
            std::uint32_t maskHash = mask32.maskHash;
            while (maskHash != 0) {
                size_type pos = BitUtils::bsf32(maskHash);
                maskHash = BitUtils::clearLowBit32(maskHash);
                size_type index = this->round_index(slot_index + (pos >> 1));
                const slot_type & target = this->get_slot(index);
                if (this->key_equal_(target.value.first, key)) {
                    this->erase_slot(index);
                    return 1;
                }
            }
            if (mask32.maskEmpty != 0) {
                return 0;
            }
            distance += kGroupWidth;
            slot_index = this->slot_next_group(slot_index);
        } while (slot_index != start_slot);

        return 0;
    }

    JSTD_FORCED_INLINE
    void erase_slot(size_type slot_index) {
        assert(index <= this->slot_capacity());
        assert(this->control_at(slot_index)->isUsed());
        size_type start_slot = slot_index;

        const group_type & group = this->get_group(start_slot);
        this->setUnusedCtrl(slot_index, kEmptyEntry);

        // Destroy slot
        this->destroy_slot(slot_index);
        assert(this->slot_size_ > 0);
        this->slot_size_--;
    }

    void swap_content(robin32_hash_map & other) {
        using std::swap;
        swap(this->groups_, other.groups());
        swap(this->group_mask_, other.group_mask());
        swap(this->slots_, other.slots());
        swap(this->slot_size_, other.slot_size());
        swap(this->slot_mask_, other.slot_mask());
        swap(this->slot_threshold_, other.slot_threshold());
        swap(this->n_mlf_, other.integral_mlf());
        swap(this->n_mlf_rev_, other.integral_mlf_rev());
    }

    void swap_policy(robin32_hash_map & other) {
        using std::swap;
        swap(this->hasher_, other.hash_function());
        swap(this->key_equal_, other.key_eq());
        if (std::allocator_traits<allocator_type>::propagate_on_container_swap::value) {
            swap(this->allocator_, other.get_allocator());
        }
        if (std::allocator_traits<mutable_allocator_type>::propagate_on_container_swap::value) {
            swap(this->mutable_allocator_, other.get_mutable_allocator());
        }
        if (std::allocator_traits<slot_allocator_type>::propagate_on_container_swap::value) {
            swap(this->slot_allocator_, other.get_slot_allocator());
        }
        if (std::allocator_traits<mutable_slot_allocator_type>::propagate_on_container_swap::value) {
            swap(this->mutable_slot_allocator_, other.get_mutable_slot_allocator());
        }
    }

    void swap_impl(robin32_hash_map & other) {
        this->swap_content(other);
        this->swap_policy(other);
    }
};

} // namespace jstd
