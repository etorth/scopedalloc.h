/*
 * =====================================================================================
 *
 *       Filename: scopedalloc.h
 *        Created: 06/10/2020 12:53:04
 *    Description: 
 *
 *        Version: 1.0
 *       Revision: none
 *       Compiler: g++ -std=c++14
 *
 *         Author: ANHONG
 *          Email:
 *   Organization:
 *
 * =====================================================================================
 */

#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cassert>
#include <vector>
#include <stdexcept>
#include "fflerror.h"

#define SCOPED_ALLOC_THROW_OVERLIVE
#define SCOPED_ALLOC_SUPPORT_OVERALIGN

// use posix_memalign()
// aligned_alloc is not standardized for compiler with c++14
#define SCOPED_ALLOC_USE_POSIX_MEMALIGN

namespace scoped_alloc
{
    constexpr bool is_power2(size_t n)
    {
        return (n > 0) && ((n & (n - 1)) == 0);
    }

    constexpr bool check_alignment(size_t alignment)
    {
        if(!is_power2(alignment)){
            return false;
        }

        if(alignment <= alignof(std::max_align_t)){
            return true;
        }

#ifdef SCOPED_ALLOC_SUPPORT_OVERALIGN
        return alignment % sizeof(void *) == 0;
#else
        return false;
#endif
    }

    template<size_t Alignment> struct aligned_buf
    {
        constexpr static size_t alignment = Alignment;
        static_assert(check_alignment(Alignment), "bad alignment");

        char * const buf  = nullptr;
        const size_t size = 0;
    };

    template<size_t Alignment = alignof(std::max_align_t)> class arena_interf
    {
        private:
            static_assert(check_alignment(Alignment), "bad alignment");

        private:
            char  *m_buf  = nullptr;
            size_t m_size = 0;

        private:
            char *m_cursor = nullptr;

        protected:
            template<size_t BufAlignment> void set_buf(const aligned_buf<BufAlignment> &buf)
            {
                static_assert(check_alignment(BufAlignment) && (BufAlignment >= Alignment), "bad aligned_buf alignment");
                if(!(buf.buf && buf.size)){
                    throw fflerror("empty aligned_buf");
                }

                m_cursor = buf.buf;
                m_buf    = buf.buf;
                m_size   = buf.size;
            }

        public:
            bool has_buf() const
            {
                return m_buf && m_size;
            }

        public:
            aligned_buf<Alignment> get_buf() const
            {
                return {m_buf, m_size};
            }

        public:
            aligned_buf<Alignment> get_buf_ex() const
            {
                if(!has_buf()){
                    throw fflerror("no valid buffer attached to arena_interf");
                }
                return get_buf();
            }

        public:
            arena_interf() = default;

        public:
            arena_interf(arena_interf &&) = delete;
            arena_interf(const arena_interf&) = delete;
            arena_interf &operator = (const arena_interf &) = delete;

        public:
            virtual ~arena_interf()
            {
                m_cursor = nullptr;
                m_buf    = nullptr;
                m_size   = 0;
            }

        public:
            size_t used() const
            {
                return static_cast<size_t>(m_cursor - get_buf_ex().buf);
            }

            float usage() const
            {
                const auto buf = get_buf_ex();
                return (static_cast<size_t>(m_cursor - buf.buf) * 1.0f) / buf.size;
            }

            void reset()
            {
                m_cursor = get_buf_ex().buf;
            }

        protected:
            bool in_buf(char *p) const
            {
                const auto buf = get_buf_ex();
                return buf.buf <= p && p <= buf.buf + buf.size;
            }

        protected:
            static size_t aligned_size(size_t byte_count) noexcept
            {
                return (byte_count + (Alignment - 1)) & ~(Alignment - 1);
            }

        public:
            template<size_t RequestAlignment> char *allocate(size_t byte_count)
            {
                static_assert(RequestAlignment <= Alignment, "alignment too small");
                detect_outlive();

                // don't throw
                // implementation defined

                if(byte_count == 0){
                    return nullptr;
                }

                const auto buf = get_buf_ex();
                const auto byte_count_aligned = aligned_size(byte_count);

                if(static_cast<decltype(byte_count_aligned)>(buf.buf + buf.size - m_cursor) >= byte_count_aligned){
                    auto r = m_cursor;
                    m_cursor += byte_count_aligned;
                    return r;
                }

                // TODO normal operator new() only guarantees alignemnt <= alignof(std::max_align_t)
                //      but class user may ask for over-aligned memory, how to handle this?
                //
                //     1. raise compilation error if asks for over-alignment
                //     2. here I try posix_memalign()/aligned_alloc(), this may waste memory, or other issue?
                //

                return dynamic_alloc(byte_count);
            }

        public:
            void deallocate(char *p, size_t byte_count) noexcept
            {
                detect_outlive();

                if(in_buf(p)){
                    if(p + aligned_size(byte_count) == m_cursor){
                        m_cursor = p;
                    }

                    else{
                        // TODO: main draw-back
                        // can't recycle memory here without extra flags
                    }
                }

                else{
                    this->free_aligned(p);
                }
            }

        public:
            virtual char *dynamic_alloc(size_t byte_count)
            {
                return alloc_aligned(byte_count);
            }

        private:
#ifdef SCOPED_ALLOC_SUPPORT_OVERALIGN
            static char *overalign_alloc(size_t byte_count)
            {
                void *aligned_ptr = nullptr;
#ifdef SCOPED_ALLOC_USE_POSIX_MEMALIGN
                if(!posix_memalign(&aligned_ptr, Alignment, byte_count)){
                    return static_cast<char *>(aligned_ptr);
                }
                throw fflerror("posix_memalign(..., alignment = %zu, byte_count = %zu) failed", Alignment, byte_count);
#else
                aligned_ptr = aligned_alloc(Alignment, aligned_size(byte_count));
                if(aligned_ptr){
                    return static_cast<char *>(aligned_ptr);
                }
                throw fflerror("aligned_alloc(alignment = %zu, byte_count = %zu) failed", Alignment, aligned_size(byte_count));
#endif
            }

            static void overalign_free(char *p)
            {
                free(p);
            }
#endif
        public:
            static char *alloc_aligned(size_t byte_count)
            {
#ifdef SCOPED_ALLOC_SUPPORT_OVERALIGN
                return overalign_alloc(byte_count);
#else
                // TODO can static assert this in constructor
                //      should we deley it till we really doing buffer allocation?
                static_assert(Alignment <= alignof(std::max_align_t), "over aligned buffer allcation is not supported");
                return static_cast<char*>(::operator new(byte_count));
#endif
            }

            static void free_aligned(char *p)
            {
                if(p){
#ifdef SCOPED_ALLOC_SUPPORT_OVERALIGN
                    overalign_free(p);
#else
                    ::operator delete(p);
#endif
                }
            }

        private:
            void detect_outlive() const
            {
                // TODO: best-effort
                //       detect_outlive() invokes undefined behavior if assertion failed
#ifdef SCOPED_ALLOC_THROW_OVERLIVE
                if(!(in_buf(m_cursor))){
                    throw fflerror("allocator has outlived arena_interf");
                }
#else
                assert(in_buf(m_cursor) && "allocator has outlived arena_interf");
#endif
            }
    };

    template<size_t ByteCount, size_t Alignment = alignof(std::max_align_t)> class fixed_arena: public scoped_alloc::arena_interf<Alignment>
    {
        private:
            alignas(Alignment) char m_storage[ByteCount];

        public:
            fixed_arena(): scoped_alloc::arena_interf<Alignment>()
            {
                this->set_buf(scoped_alloc::aligned_buf<Alignment>{m_storage, ByteCount});
            }
    };

    template<size_t Alignment = alignof(std::max_align_t)> class dynamic_arena: public scoped_alloc::arena_interf<Alignment>
    {
        public:
            dynamic_arena(size_t byte_count = 0): scoped_alloc::arena_interf<Alignment>()
            {
                if(!byte_count){
                    return;
                }
                alloc(byte_count);
            }

            template<size_t BufAlignment> dynamic_arena(const scoped_alloc::aligned_buf<BufAlignment> &buf)
                : scoped_alloc::arena_interf<Alignment>()
            {
                set_buf(buf);
            }

        public:
            ~dynamic_arena() override
            {
                if(this->has_buf()){
                    this->free_aligned(this->get_buf().buf);
                }
            }

        public:
            void alloc(size_t byte_count)
            {
                if(byte_count == 0){
                    throw fflerror("invalid allocation: byte_count = 0");
                }

                if(this->has_buf()){
                    this->free_aligned(this->get_buf().buf);
                }
                this->set_buf(scoped_alloc::aligned_buf<Alignment>{scoped_alloc::arena_interf<Alignment>::alloc_aligned(byte_count), byte_count});
            }
    };

    template<class T, size_t Alignment = alignof(std::max_align_t)> class allocator
    {
        public:
            using value_type = T;
            template <class U, size_t A> friend class scoped_alloc::allocator;

        public:
            template <class UpperType> struct rebind
            {
                using other = allocator<UpperType, Alignment>;
            };

        private:
            arena_interf<Alignment> &m_arena;

        public:
            allocator(const allocator &) = default;
            allocator &operator=(const allocator &) = delete;

        public:
            allocator(arena_interf<Alignment> &arena_ref) noexcept
                : m_arena(arena_ref)
            {}

        public:
            template<class U> allocator(const allocator<U, Alignment>& alloc_ref) noexcept
                : m_arena(alloc_ref.m_arena)
            {}

        public:
            T* allocate(size_t n)
            {
                return reinterpret_cast<T *>(m_arena.template allocate<alignof(T)>(n * sizeof(T)));
            }

            void deallocate(T *p, size_t n) noexcept
            {
                m_arena.deallocate(reinterpret_cast<char *>(p), n * sizeof(T));
            }

        public:
            template<typename T2, size_t A2> bool operator == (const scoped_alloc::allocator<T2, A2> &parm) noexcept
            {
                return Alignment == A2 && &(this->m_arena) == &(parm.m_arena);
            }

            template<typename T2, size_t A2> bool operator != (const scoped_alloc::allocator<T2, A2> &parm) noexcept
            {
                return !(*this == parm);
            }
    };
}

template<typename T, size_t N> class svobuf
{
    private:
        scoped_alloc::fixed_arena<N * sizeof(T), alignof(T)> m_arena;

    public:
        std::vector<T, scoped_alloc::allocator<T, alignof(T)>> c;

    public:
        svobuf()
            : c(typename decltype(c)::allocator_type(m_arena))
        {
            // immediately use all static buffer
            // 1. to prevent push_back() to allocate on heap, best effort
            // 2. to prevent waste of memory
            //
            //     svobuf<int, 4> buf;
            //     auto buf_cp = buf.c;
            //
            //     buf_cp.push_back(1);
            //     buf_cp.push_back(2);
            //
            // here buf has ran out of all static buffer
            // the copy constructed buf_cp will always allocates memory on heap
            //
            // ill-formed code:
            //
            //    auto f()
            //    {
            //        svobuf<int, 4> buf;
            //
            //        buf.c.push_back(0);
            //        buf.c.push_back(1);
            //        ...
            //
            //        return buf.c;
            //    }
            //
            // return a copy of buf.c also copies its scoped allocator
            // this causes dangling reference

            c.reserve(N);
            if(c.capacity() > N){
                throw fflerror("allocate initial buffer dynamically");
            }
        }

    public:
        constexpr size_t svocap() const
        {
            return N;
        }
};
