#pragma once
#include <type_traits>
//implement and stollen from
// http://open-std.org/JTC1/SC22/WG21/docs/papers/2019/p1895r0.pdf
namespace cor3ntin::corio {
    namespace __tag_invoke_fn_ns
    {
        void tag_invoke() = delete;

        struct __tag_invoke_fn
        {
            template<typename _Tag, typename... _Args>
                requires requires (_Tag __tag, _Args&&... __args) {
                    tag_invoke((_Tag&&)__tag, (_Args&&)__args...);
                }
            constexpr auto operator()(_Tag __tag, _Args&&... __args) const
                noexcept(noexcept(tag_invoke((_Tag&&)__tag, (_Args&&)__args...)))
                -> decltype(auto) {
                return tag_invoke((_Tag&&)__tag, (_Args&&)__args...);
            }
        };
    }

    inline namespace __tag_invoke_ns
    {
        inline constexpr __tag_invoke_fn_ns::__tag_invoke_fn tag_invoke = {};
    }

    template<typename _Tag, typename... _Args>
    concept tag_invocable =
        requires(_Tag && tag, _Args&&... args) {
            cor3ntin::corio::tag_invoke((_Tag&&)tag, (_Args&&)args...);
        };

    template<typename _Tag, typename... _Args>
    concept nothrow_tag_invocable =
        tag_invocable<_Tag, _Args...> &&
        requires(_Tag && tag, _Args&&... args) {
            { cor3ntin::corio::tag_invoke((_Tag&&)tag, (_Args&&)args...) } noexcept;
        };

    template<typename _Tag, typename... _Args>
    using tag_invoke_result = std::invoke_result<decltype(cor3ntin::corio::tag_invoke), _Tag, _Args...>;

    template<typename _Tag, typename... _Args>
    using tag_invoke_result_t = std::invoke_result_t<decltype(cor3ntin::corio::tag_invoke), _Tag, _Args...>;

    template<auto& _Tag>
    using tag_t = std::decay_t<decltype(_Tag)>;
}