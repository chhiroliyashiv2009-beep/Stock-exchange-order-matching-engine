#pragma once

#include <bits/stdc++.h>
namespace exchange::core
{

    using Price = std::int64_t; // generally it is in float but wath we do is a50.25==== 1502500  0.00001=1
    using Quantity = std::uint32_t;
    using OrderId = std::uint64_t;
    using SequenceNumber = std::uint64_t;
    using Timestamp = std::uint64_t; // wall clock & logical timestamps
    using ParticipantId = std::uint32_t;
    using MatchId = std::uint64_t;
    inline constexpr Price PRICE_SCALE = 10'000;

    enum class Side : std::uint8_t
    {
        BUY,
        SELL,
    };
    enum class OrderType : std::uint8_t
    {
        LIMIT,
        MARKET,
        IOC, // imidiate or cancel
        FOK, // fill or kill
        GTC,
        STOP,
        STOP_LIMIT,
        ICEBERG,
        POST_ONLY,
    };
    enum class OrderStatus : std::uint8_t
    {
        NEW,
        ACCEPTED,
        PARTIALLY_FILLED,
        FILLED,
        CANCELED,
    };
    using Symbol = std::array<char, 8>;
    constexpr Symbol make_symbol(std::string_view text) noexcept
    {
        Symbol symbol{};
        const auto length = std::min(symbol.size(), text.size());
        for (std::size_t index = 0; index < length; ++index)
        {
            symbol[index] = text[index];
        }
        return symbol; 
    }

    constexpr std::string_view symbol_view(const Symbol &symbol) noexcept
    {
        std::size_t length = 0;
        while (length < symbol.size() && symbol[length] != '\0')
        {
            ++length;
        }
        return std::string_view(symbol.data(), length);
    }
    struct SymbolLess
    {
        constexpr bool operator()(const Symbol &lhs,
                                  const Symbol &rhs) const noexcept
        {
            return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(),
                                                rhs.end());
        }
    };

    static_assert(std::is_trivially_copyable_v<Price>);
    static_assert(std::is_trivially_copyable_v<Quantity>);
    static_assert(std::is_trivially_copyable_v<OrderId>);
    static_assert(std::is_trivially_copyable_v<SequenceNumber>);
    static_assert(std::is_trivially_copyable_v<Timestamp>);
    static_assert(std::is_trivially_copyable_v<ParticipantId>);
    static_assert(std::is_trivially_copyable_v<MatchId>);
    static_assert(std::is_trivially_copyable_v<Side>);
    static_assert(std::is_trivially_copyable_v<OrderType>);
    static_assert(std::is_trivially_copyable_v<OrderStatus>);
    static_assert(std::is_trivially_copyable_v<Symbol>);
}