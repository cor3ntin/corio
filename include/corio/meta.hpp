#pragma once
#include <stl2/concepts.hpp>

namespace cor3ntin::corio {

namespace concepts = std::experimental::ranges;

namespace details {

    template <class T>
    concept nothrow_move_or_copy_constructible =
        std::is_nothrow_move_constructible_v<T> || concepts::copy_constructible<T>;

    template <typename UniqueTypes, typename UnprocessedTypes>
    struct deduplicate_impl {
        using type = UniqueTypes;
    };

    template <template <typename...> class List, typename... UniqueTs, typename Next,
              typename... Rest>
    struct deduplicate_impl<List<UniqueTs...>, List<Next, Rest...>>
        : deduplicate_impl<std::conditional_t<std::disjunction_v<std::is_same<UniqueTs, Next>...>,
                                              List<UniqueTs...>, List<UniqueTs..., Next>>,
                           List<Rest...>> {};

    template <typename Types>
    struct deduplicate;

    template <template <typename...> class List, typename... Ts>
    struct deduplicate<List<Ts...>> : deduplicate_impl<List<>, List<Ts...>> {};

    template <typename Types>
    using deduplicate_t = typename deduplicate<Types>::type;

    template <typename... Values>
    struct single_type {
        // empty so we are SFINAE friendly.
    };

    template <typename T>
    struct single_type<T> {
        using type = T;
    };

    template <typename... Types>
    struct single_value_type {
        using type = std::tuple<Types...>;
    };

    template <typename T>
    struct single_value_type<T> {
        using type = T;
    };

    template <>
    struct single_value_type<> {
        using type = void;
    };
    template <typename... Args>
    struct is_empty_list : std::bool_constant<(sizeof...(Args) == 0)> {};

    struct empty_result_t {};

    template <typename T>
    using non_void_t = std::conditional_t<std::is_void_v<T>, empty_result_t, T>;

}  // namespace details

}  // namespace cor3ntin::corio