// Copyright (c) 2023, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#ifndef MAMBA_SPECS_VERSION_HPP
#define MAMBA_SPECS_VERSION_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "mamba/specs/error.hpp"

namespace mamba::specs
{

    /**
     * A succession of a number and a lowercase literal.
     *
     * Comparison is done lexicographically, with the number first and the literal second.
     * Certain literals have special meaning:
     * "*" < "dev" < "_"< any other literal < "" < "post"
     */
    class VersionPartAtom
    {
    public:

        VersionPartAtom() noexcept = default;
        VersionPartAtom(std::size_t numeral) noexcept;
        VersionPartAtom(std::size_t numeral, std::string_view literal);
        // The use of a template is only meant to prevent ambiguous conversions
        template <typename Char>
        VersionPartAtom(std::size_t numeral, std::basic_string<Char> literal);

        [[nodiscard]] auto numeral() const noexcept -> std::size_t;
        [[nodiscard]] auto literal() const& noexcept -> const std::string&;
        auto literal() && noexcept -> std::string;

        [[nodiscard]] auto str() const -> std::string;

        auto operator==(const VersionPartAtom& other) const -> bool;
        auto operator!=(const VersionPartAtom& other) const -> bool;
        auto operator<(const VersionPartAtom& other) const -> bool;
        auto operator<=(const VersionPartAtom& other) const -> bool;
        auto operator>(const VersionPartAtom& other) const -> bool;
        auto operator>=(const VersionPartAtom& other) const -> bool;

    private:

        // Stored in decreasing size order for performance
        std::string m_literal = "";
        std::size_t m_numeral = 0;
    };

    extern template VersionPartAtom::VersionPartAtom(std::size_t, std::string);

    /**
     * A sequence of VersionPartAtom meant to represent a part of a version (e.g. major, minor).
     *
     * Version parts can have a arbitrary number of atoms, such as {0, "post"} {1, "dev"}
     * in 0post1dev
     *
     * @see  Version::parse for how this is computed from strings.
     * @todo Use a small vector of expected size 1 if performance ar not good enough.
     */
    using VersionPart = std::vector<VersionPartAtom>;

    /**
     * A sequence of VersionPart meant to represent all parts of a version.
     *
     * CommonVersion are composed of an arbitrary positive number parts, such as major, minor.
     * They are typically separated by dots, for instance the three parts in 3.0post1dev.4 are
     * {{3, ""}}, {{0, "post"}, {1, "dev"}}, and {{4, ""}}.
     *
     * @see  Version::parse for how this is computed from strings.
     * @todo Use a small vector of expected size 4 if performance ar not good enough.
     */
    using CommonVersion = std::vector<VersionPart>;

    /**
     * A version according to Conda specifications.
     *
     * A version is composed of
     * - A epoch number, usually 0;
     * - A regular version,
     * - An optional local.
     * These elements are used to lexicographicaly compare two versions.
     *
     * @see https://github.com/conda/conda/blob/main/conda/models/version.py
     */
    class Version
    {
    public:

        static constexpr char epoch_delim = '!';
        static constexpr char local_delim = '+';
        static constexpr char part_delim = '.';
        static constexpr char part_delim_alt = '-';
        static constexpr char part_delim_special = '_';

        static auto parse(std::string_view str) -> expected_parse_t<Version>;

        /** Construct version ``0.0``. */
        Version() noexcept = default;
        Version(std::size_t epoch, CommonVersion version, CommonVersion local = {}) noexcept;

        [[nodiscard]] auto epoch() const noexcept -> std::size_t;
        [[nodiscard]] auto version() const noexcept -> const CommonVersion&;
        [[nodiscard]] auto local() const noexcept -> const CommonVersion&;

        /**
         * A string representation of the version.
         *
         * May not always be the same as the parsed string (due to reconstruction) but reparsing
         * this string will give the same version.
         * ``v == Version::parse(v.str())``.
         */
        [[nodiscard]] auto str() const -> std::string;

        /**
         * A string truncated of extended representation of the version.
         *
         * Represent the string with the desired number of parts.
         * If the actual number of parts is larger, then the string is truncated.
         * If the actual number of parts is smalle, then the string is expanded with zeros.
         */
        [[nodiscard]] auto str(std::size_t level) const -> std::string;

        /**
         * String representation that treats ``*`` as glob pattern.
         *
         * Instead of printing them as ``0*`` (as a special literal), it formats them as ``*``.
         * In full, a version like ``*.1.*`` will print as such instead of ``0*.1.0*``.
         */
        [[nodiscard]] auto str_glob() const -> std::string;

        [[nodiscard]] auto operator==(const Version& other) const -> bool;
        [[nodiscard]] auto operator!=(const Version& other) const -> bool;
        [[nodiscard]] auto operator<(const Version& other) const -> bool;
        [[nodiscard]] auto operator<=(const Version& other) const -> bool;
        [[nodiscard]] auto operator>(const Version& other) const -> bool;
        [[nodiscard]] auto operator>=(const Version& other) const -> bool;

        /**
         * Return true if this version starts with the other prefix.
         *
         * For instance 1.2.3 starts with 1.2 but not the opposite.
         * Because Conda versions can contain an arbitrary number of segments, some of which
         * with alpha releases, this function cannot be written as a comparison.
         * One would need to comoare with a version with infinitely pre-release segments.
         */
        [[nodiscard]] auto starts_with(const Version& prefix) const -> bool;

        /**
         * Return true if this version is a compatible upgrade to the given one.
         *
         * For instance 1.3.1 is compatible with 1.2.1 at level 0 (first component `1 == 1``),
         * at level 1 (second component `` 3 >= 2``), but not at level two (because the second
         * component is strictly larger ``3 > 2``).
         * Compatible versions are always smaller than the current version.
         */
        [[nodiscard]] auto compatible_with(const Version& older, std::size_t level) const -> bool;

    private:

        // Stored in decreasing size order for performance
        CommonVersion m_version = {};
        CommonVersion m_local = {};
        std::size_t m_epoch = 0;
    };

    namespace version_literals
    {
        auto operator""_v(const char* str, std::size_t len) -> Version;
    }
}

template <>
struct fmt::formatter<mamba::specs::VersionPartAtom>
{
    auto parse(format_parse_context& ctx) -> decltype(ctx.begin());

    auto format(const ::mamba::specs::VersionPartAtom atom, format_context& ctx) const
        -> decltype(ctx.out());
};

template <>
struct fmt::formatter<mamba::specs::Version>
{
    enum struct FormatType
    {
        Normal,
        /**
         * The Glob pattern, as used internally ``VersionPredicate``, lets you treat ``*`` as a
         * glob pattern instead of the special character.
         * It lets you format ``*.*`` as such instead of ``0*.0*``.
         */
        Glob,
    };

    std::optional<std::size_t> m_level;
    FormatType m_type = FormatType::Normal;

    auto parse(format_parse_context& ctx) -> decltype(ctx.begin());

    auto format(const ::mamba::specs::Version v, format_context& ctx) const -> decltype(ctx.out());
};

#endif
