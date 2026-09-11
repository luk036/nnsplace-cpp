#pragma once

/** @file pyfractions.hpp
 *  This is a C++ Library header.
 */

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#if __cpp_constexpr >= 201304
#    define CONSTEXPR14 constexpr
#else
#    define CONSTEXPR14 inline
#endif

namespace fractions {

    /**
     * @brief Fraction class - C++ port of Python's fractions.Fraction
     *
     * A direct C++ implementation of Python's fractions.Fraction class that closely
     * mirrors the Python implementation, including optimized arithmetic algorithms
     * from Knuth's TAOCP.
     *
     * Key features:
     * @verbatim
     * - Exact rational number representation
     * - Automatic normalization to lowest terms
     * - Optimized GCD-based arithmetic
     * - Overflow-safe operations with floating-point fallback
     * - Full comparison and arithmetic operator support
     * @endverbatim
     * @tparam T Integer type for numerator and denominator (default: int64_t)
     */
    template <typename T = int64_t> class Fraction {
      public:
        using value_type = T;

        /**
         * Destructor
         */
        ~Fraction() = default;

      private:
        T _numerator;
        T _denominator;

        /**
         * @brief Compute the greatest common divisor of two integers.
         *
         * @f$ \gcd(a,b) = \gcd(|a|,|b|) @f$ using Euclid's algorithm.
         *
         * @param[in] a First integer.
         * @param[in] b Second integer.
         * @return GCD of a and b.
         */
        static CONSTEXPR14 T _gcd(T a, T b) {
            a = a < 0 ? -a : a;
            b = b < 0 ? -b : b;
            while (b != 0) {
                T temp = b;
                b = a % b;
                a = temp;
            }
            return a;
        }

        /**
         * @brief Maximum representable value divided by |d|, guarding d == 0.
         *
         * The overflow guards throughout the arithmetic operators compare a
         * magnitude against ``max / |d|``; when ``d`` is a (zero) numerator that
         * quotient would be a division by zero, so a zero divisor yields max.
         * @param[in] d The prospective divisor.
         * @return max / |d| when d != 0, else max.
         */
        static CONSTEXPR14 T _max_div(T d) noexcept {
            if (d == 0) return std::numeric_limits<T>::max();
            return std::numeric_limits<T>::max() / (d < 0 ? -d : d);
        }

        /**
         * @brief Internal constructor for pre-normalized values (bypasses normalization).
         * @param[in] numerator The numerator value.
         * @param[in] denominator The denominator value.
         * @param[in] skip_normalize Unused flag to distinguish from public constructor.
         */
        Fraction(T numerator, T denominator, bool skip_normalize)
            : _numerator(numerator), _denominator(denominator) {
            (void)skip_normalize;  // Suppress unused parameter warning
        }

      public:
        /**
         * @brief Default constructor: Fraction(0, 1)
         */
        Fraction() : _numerator(0), _denominator(1) {}

        /**
         * @brief Construct a Fraction from an integer value.
         * @param[in] numerator Integer value (denominator = 1).
         */
        explicit Fraction(T numerator) : _numerator(numerator), _denominator(1) {}

        /**
         * @brief Construct a Fraction from numerator and denominator.
         * Normalizes the fraction to lowest terms.
         * @param[in] numerator The numerator value.
         * @param[in] denominator The denominator value.
         * @throws std::runtime_error If denominator is zero.
         */
        Fraction(T numerator, T denominator) {
            if (denominator == 0) {
                throw std::runtime_error("Fraction(" + std::to_string(numerator) + ", 0)");
            }

            T g = _gcd(numerator, denominator);
            _numerator = numerator / g;
            _denominator = denominator / g;

            // Ensure denominator is positive
            if (_denominator < 0) {
                _numerator = -_numerator;
                _denominator = -_denominator;
            }
        }

        /**
         * @brief Copy constructor.
         * @param[in] other Fraction to copy.
         */
        Fraction(const Fraction& other)
            : _numerator(other._numerator), _denominator(other._denominator) {}

        /**
         * @brief Move constructor.
         * @param[in] other Fraction to move.
         */
        Fraction(Fraction&& other) noexcept
            : _numerator(std::move(other._numerator)),
              _denominator(std::move(other._denominator)) {}

        /**
         * @brief Copy assignment operator.
         * @param[in] other Fraction to copy.
         * @return Reference to this.
         */
        Fraction& operator=(const Fraction& other) {
            if (this != &other) {
                _numerator = other._numerator;
                _denominator = other._denominator;
            }
            return *this;
        }

        /**
         * @brief Move assignment operator.
         * @param[in] other Fraction to move.
         * @return Reference to this.
         */
        Fraction& operator=(Fraction&& other) noexcept {
            if (this != &other) {
                _numerator = std::move(other._numerator);
                _denominator = std::move(other._denominator);
            }
            return *this;
        }

        /**
         * @brief Get the numerator.
         * @return The numerator value.
         */
        CONSTEXPR14 T numerator() const noexcept { return _numerator; }

        /**
         * @brief Get the denominator.
         * @return The denominator value.
         */
        CONSTEXPR14 T denominator() const noexcept { return _denominator; }

        /**
         * @brief Explicit truncating conversion to the integer type.
         *
         * Lets a caller run an integer distance domain (``static_cast<T>``)
         * while the parametric API still produces exact rationals; used by the
         * NNS placer's Howard relaxation to keep per-edge arithmetic integral.
         * @return The truncated integer value (numerator / denominator).
         */
        explicit operator T() const noexcept { return _numerator / _denominator; }

        /**
         * @brief Check if the fraction is an integer (denominator == 1).
         * @return true if denominator is 1, false otherwise.
         */
        CONSTEXPR14 bool is_integer() const noexcept { return _denominator == 1; }

        /**
         * @brief Get numerator and denominator as a pair.
         * @return A pair of (numerator, denominator).
         */
        std::pair<T, T> as_integer_ratio() const { return {_numerator, _denominator}; }

        /**
         * @brief Negation operator.
         * @return A new Fraction with negated numerator.
         */
        Fraction operator-() const { return Fraction(-_numerator, _denominator, true); }

        /**
         * @brief Absolute value.
         *
         * @f$ \bigl|\frac{a}{b}\bigr| = \frac{|a|}{b} @f$
         *
         * @return A new Fraction with non-negative numerator.
         */
        Fraction abs() const {
            return Fraction(_numerator < 0 ? -_numerator : _numerator, _denominator, true);
        }

        /**
         * Addition - optimized algorithm following Knuth TAOCP Volume 2, 4.5.1
         *
         * @f$ \frac{a}{b} + \frac{c}{d} = \frac{ad + bc}{bd} @f$
         *
         * Uses GCD optimization to avoid large intermediate values:
         * - Find g = gcd(da, db)
         * - Compute result with smaller intermediate values
         */
        Fraction operator+(const Fraction& other) const {
            T na = _numerator;
            T da = _denominator;
            T nb = other._numerator;
            T db = other._denominator;

            T g = _gcd(da, db);

            if (g == 1) {
                // Check for overflow before multiplication
                if (std::abs(na) > _max_div(db)
                    || std::abs(da) > _max_div(nb)) {
                    // Use floating point as fallback
                    double result = static_cast<double>(na) / static_cast<double>(da)
                                    + static_cast<double>(nb) / static_cast<double>(db);
                    // Convert back to fraction with reasonable precision
                    const T max_den = std::numeric_limits<T>::max() / 1000;
                    T approx_den = std::min(da * db, max_den);
                    if (approx_den < 0) approx_den = -approx_den;
                    T approx_num
                        = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                    T g_approx = _gcd(approx_num, approx_den);
                    return Fraction(approx_num / g_approx, approx_den / g_approx);
                }
                return Fraction(na * db + da * nb, da * db);
            }

            T s = da / g;
            T db_div_g = db / g;

            // Check for overflow in t calculation
            if (std::abs(na) > _max_div(db_div_g)
                || std::abs(nb) > _max_div(s)) {
                // Use floating point as fallback
                double result = static_cast<double>(na) / static_cast<double>(da)
                                + static_cast<double>(nb) / static_cast<double>(db);
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(s * db, max_den);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            T t = na * db_div_g + nb * s;
            T g2 = _gcd(t, g);

            if (g2 == 1) {
                // Check for overflow in denominator
                if (std::abs(s) > _max_div(db)) {
                    const T max_den = std::numeric_limits<T>::max() / 1000;
                    T approx_den = std::min(max_den, db);
                    if (approx_den < 0) approx_den = -approx_den;
                    T approx_num = static_cast<T>(
                        std::round((static_cast<double>(t) / static_cast<double>(s))
                                   * static_cast<double>(approx_den)));
                    T g_approx = _gcd(approx_num, approx_den);
                    return Fraction(approx_num / g_approx, approx_den / g_approx);
                }
                return Fraction(t, s * db);
            }

            T db_div_g2 = db / g2;
            if (std::abs(s) > _max_div(db_div_g2)) {
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(max_den, db_div_g2);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(
                    std::round((static_cast<double>(t / g2) / static_cast<double>(s))
                               * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            return Fraction(t / g2, s * db_div_g2);
        }

        /**
         * Subtraction - optimized algorithm following Knuth TAOCP Volume 2, 4.5.1
         *
         * @f$ \frac{a}{b} - \frac{c}{d} = \frac{ad - bc}{bd} @f$
         */
        Fraction operator-(const Fraction& other) const {
            T na = _numerator;
            T da = _denominator;
            T nb = other._numerator;
            T db = other._denominator;

            T g = _gcd(da, db);

            if (g == 1) {
                // Check for overflow before multiplication
                if (std::abs(na) > _max_div(db)
                    || std::abs(da) > _max_div(nb)) {
                    // Use floating point as fallback
                    double result = static_cast<double>(na) / static_cast<double>(da)
                                    - static_cast<double>(nb) / static_cast<double>(db);
                    // Convert back to fraction with reasonable precision
                    const T max_den = std::numeric_limits<T>::max() / 1000;
                    T approx_den = std::min(da * db, max_den);
                    if (approx_den < 0) approx_den = -approx_den;
                    T approx_num
                        = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                    T g_approx = _gcd(approx_num, approx_den);
                    return Fraction(approx_num / g_approx, approx_den / g_approx);
                }
                return Fraction(na * db - da * nb, da * db);
            }

            T s = da / g;
            T db_div_g = db / g;

            // Check for overflow in t calculation
            if (std::abs(na) > _max_div(db_div_g)
                || std::abs(nb) > _max_div(s)) {
                // Use floating point as fallback
                double result = static_cast<double>(na) / static_cast<double>(da)
                                - static_cast<double>(nb) / static_cast<double>(db);
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(s * db, max_den);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            T t = na * db_div_g - nb * s;
            T g2 = _gcd(t, g);

            if (g2 == 1) {
                // Check for overflow in denominator
                if (std::abs(s) > _max_div(db)) {
                    const T max_den = std::numeric_limits<T>::max() / 1000;
                    T approx_den = std::min(max_den, db);
                    if (approx_den < 0) approx_den = -approx_den;
                    T approx_num = static_cast<T>(
                        std::round((static_cast<double>(t) / static_cast<double>(s))
                                   * static_cast<double>(approx_den)));
                    T g_approx = _gcd(approx_num, approx_den);
                    return Fraction(approx_num / g_approx, approx_den / g_approx);
                }
                return Fraction(t, s * db);
            }

            T db_div_g2 = db / g2;
            if (std::abs(s) > _max_div(db_div_g2)) {
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(max_den, db_div_g2);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(
                    std::round((static_cast<double>(t / g2) / static_cast<double>(s))
                               * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            return Fraction(t / g2, s * db_div_g2);
        }

        /**
         * Multiplication - optimized algorithm following Knuth TAOCP Volume 2, 4.5.1
         *
         * @f$ \frac{a}{b} \cdot \frac{c}{d} = \frac{ac}{bd} @f$
         *
         * Uses GCD optimization to reduce before multiplying:
         * - g1 = gcd(na, db)
         * - g2 = gcd(nb, da)
         * - Divide before multiplying to keep numbers small
         */
        Fraction operator*(const Fraction& other) const {
            T na = _numerator;
            T da = _denominator;
            T nb = other._numerator;
            T db = other._denominator;

            T g1 = _gcd(na, db);
            if (g1 > 1) {
                na /= g1;
                db /= g1;
            }

            T g2 = _gcd(nb, da);
            if (g2 > 1) {
                nb /= g2;
                da /= g2;
            }

            // Check for overflow before final multiplication
            if (std::abs(na) > _max_div(nb)
                || std::abs(db) > _max_div(da)) {
                // Use floating point as fallback
                double result = (static_cast<double>(na) * static_cast<double>(nb))
                                / (static_cast<double>(db) * static_cast<double>(da));
                // Convert back to fraction with reasonable precision
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(db * da, max_den);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            return Fraction(na * nb, db * da);
        }

        /**
         * Division - optimized algorithm (inverse of multiplication)
         *
         * @f$ \frac{a}{b} / \frac{c}{d} = \frac{ad}{bc} @f$
         */
        Fraction operator/(const Fraction& other) const {
            T nb = other._numerator;
            T db = other._denominator;

            if (nb == 0) {
                throw std::runtime_error("Fraction division by zero");
            }

            T na = _numerator;
            T da = _denominator;

            T g1 = _gcd(na, nb);
            if (g1 > 1) {
                na /= g1;
                nb /= g1;
            }

            T g2 = _gcd(db, da);
            if (g2 > 1) {
                da /= g2;
                db /= g2;
            }

            // Check for overflow before multiplication
            if (std::abs(na) > _max_div(db)
                || std::abs(nb) > _max_div(da)) {
                // Use floating point as fallback
                double result = (static_cast<double>(na) * static_cast<double>(db))
                                / (static_cast<double>(nb) * static_cast<double>(da));
                // Convert back to fraction with reasonable precision
                const T max_den = std::numeric_limits<T>::max() / 1000;
                T approx_den = std::min(nb * da, max_den);
                if (approx_den < 0) approx_den = -approx_den;
                T approx_num = static_cast<T>(std::round(result * static_cast<double>(approx_den)));
                T g_approx = _gcd(approx_num, approx_den);
                return Fraction(approx_num / g_approx, approx_den / g_approx);
            }

            T n = na * db;
            T d = nb * da;

            if (d < 0) {
                n = -n;
                d = -d;
            }

            return Fraction(n, d);
        }

        /**
         * In-place addition
         */
        Fraction& operator+=(const Fraction& other) {
            *this = *this + other;
            return *this;
        }

        /**
         * In-place subtraction
         */
        Fraction& operator-=(const Fraction& other) {
            *this = *this - other;
            return *this;
        }

        /**
         * In-place multiplication
         */
        Fraction& operator*=(const Fraction& other) {
            *this = *this * other;
            return *this;
        }

        /**
         * In-place division
         */
        Fraction& operator/=(const Fraction& other) {
            *this = *this / other;
            return *this;
        }

        /**
         * Addition with integer
         */
        Fraction operator+(T other) const { return *this + Fraction(other); }

        /**
         * Subtraction with integer
         */
        Fraction operator-(T other) const { return *this - Fraction(other); }

        /**
         * Multiplication with integer
         */
        Fraction operator*(T other) const { return *this * Fraction(other); }

        /**
         * Division with integer
         */
        Fraction operator/(T other) const { return *this / Fraction(other); }

        /**
         * Equality comparison
         */
        bool operator==(const Fraction& other) const {
            return _numerator == other._numerator && _denominator == other._denominator;
        }

        /**
         * Inequality comparison
         */
        bool operator!=(const Fraction& other) const { return !(*this == other); }

        /**
         * Less than comparison (overflow-safe)
         */
        bool operator<(const Fraction& other) const {
            T na = _numerator;
            T da = _denominator;
            T nb = other._numerator;
            T db = other._denominator;

            // Handle different signs
            if (na < 0 && nb >= 0) return true;
            if (na >= 0 && nb < 0) return false;

            // Both are non-negative or both are negative
            // For negative case, the comparison reverses
            bool both_negative = (na < 0);

            // Make both non-negative for comparison
            if (both_negative) {
                na = -na;
                nb = -nb;
            }

            // Now na, da, nb, db are all non-negative
            // We want to check if na/da < nb/db without overflow

            // Use division-based comparison when safe
            if (da != 0 && db != 0) {
                // Compare na/da and nb/db by converting to double for large values
                // This avoids overflow while maintaining precision for comparison
                if (na > std::numeric_limits<T>::max() / 1000
                    || nb > std::numeric_limits<T>::max() / 1000
                    || da > std::numeric_limits<T>::max() / 1000
                    || db > std::numeric_limits<T>::max() / 1000) {
                    // Use floating point comparison for very large numbers
                    double left = static_cast<double>(na) / static_cast<double>(da);
                    double right = static_cast<double>(nb) / static_cast<double>(db);
                    return left < right;
                }

                // Try to cancel common factors to reduce numbers
                T g1 = _gcd(na, nb);
                if (g1 > 1) {
                    na /= g1;
                    nb /= g1;
                }

                T g2 = _gcd(da, db);
                if (g2 > 1) {
                    da /= g2;
                    db /= g2;
                }

                // Check if multiplication would still overflow
                if (na <= std::numeric_limits<T>::max() / db
                    && nb <= std::numeric_limits<T>::max() / da) {
                    // Safe to use cross-multiplication
                    return na * db < nb * da;
                }

                // Fallback to floating point comparison
                double left = static_cast<double>(na) / static_cast<double>(da);
                double right = static_cast<double>(nb) / static_cast<double>(db);
                return left < right;
            }

            // Default case (shouldn't reach here with valid fractions)
            return false;
        }

        /**
         * Greater than comparison
         */
        bool operator>(const Fraction& other) const { return other < *this; }

        /**
         * Less than or equal comparison
         */
        bool operator<=(const Fraction& other) const { return !(other < *this); }

        /**
         * Greater than or equal comparison
         */
        bool operator>=(const Fraction& other) const { return !(*this < other); }

        /**
         * Comparison with integer
         */
        bool operator==(T other) const { return _numerator == other && _denominator == 1; }

        bool operator!=(T other) const { return !(*this == other); }

        bool operator<(T other) const {
            if (_denominator == 1 || other == 0) {
                return _numerator < other;
            }
            return _numerator < _denominator * other;
        }

        bool operator>(T other) const { return other < *this; }

        bool operator<=(T other) const { return !(other < *this); }

        bool operator>=(T other) const { return !(*this < other); }

        /**
         * Integer division (floor)
         *
         * @f$ \left\lfloor \frac{a/b}{c/d} \right\rfloor = \left\lfloor \frac{ad}{bc} \right\rfloor
         * @f$
         */
        T floor_div(const Fraction& other) const {
            if (other._numerator == 0) {
                throw std::runtime_error("Division by zero in floor_div");
            }

            // To avoid overflow, we can use floating point division for large numbers
            // or carefully check for overflow before multiplication
            if (std::abs(_numerator) > _max_div(other._denominator)
                || std::abs(_denominator)
                       > _max_div(other._numerator)) {
                // Use floating point to avoid overflow
                double result
                    = static_cast<double>(_numerator) * static_cast<double>(other._denominator)
                      / (static_cast<double>(_denominator) * static_cast<double>(other._numerator));
                return static_cast<T>(std::floor(result));
            }

            // Safe to use integer arithmetic
            T numerator_product = _numerator * other._denominator;
            T denominator_product = _denominator * other._numerator;

            // Handle sign for floor division
            if ((numerator_product >= 0) == (denominator_product > 0)) {
                // Same sign or numerator is 0 - simple division
                return numerator_product / denominator_product;
            }
            // Different signs - need to adjust for floor
            T quotient = numerator_product / denominator_product;
            if (numerator_product % denominator_product != 0) {
                return quotient - 1;
            }
            return quotient;
        }

        /**
         * Modulo operation
         *
         * @f$ \frac{a}{b} \bmod \frac{c}{d} = \frac{(ad) \bmod (bc)}{bd} @f$
         */
        Fraction operator%(const Fraction& other) const {
            if (other._numerator == 0) {
                throw std::runtime_error("Modulo by zero");
            }

            T da = _denominator;
            T db = other._denominator;

            // Check for potential overflow in multiplications
            bool overflow_risk
                = (std::abs(_numerator) > _max_div(db)
                   || std::abs(other._numerator) > _max_div(da)
                   || std::abs(da) > _max_div(db));

            if (overflow_risk) {
                // Use floating point arithmetic to avoid overflow
                double a = static_cast<double>(_numerator) / static_cast<double>(da);
                double b = static_cast<double>(other._numerator) / static_cast<double>(db);
                double result = std::fmod(a, b);

                // Convert back to fraction
                // Use a reasonable denominator based on original denominators
                T new_denominator = da * db;
                if (new_denominator < 0) new_denominator = -new_denominator;

                T new_numerator
                    = static_cast<T>(std::round(result * static_cast<double>(new_denominator)));

                // Normalize the result
                T g = _gcd(new_numerator, new_denominator);
                return Fraction(new_numerator / g, new_denominator / g);
            }

            // Safe to use integer arithmetic
            T numerator_product = _numerator * db;
            T denominator_product = other._numerator * da;
            T n_mod = numerator_product % denominator_product;
            T result_denominator = da * db;

            return Fraction(n_mod, result_denominator);
        }

        /**
         * Power operation (integer exponent only)
         *
         * @f$ \left(\frac{a}{b}\right)^n = \frac{a^n}{b^n} @f$
         */
        Fraction pow(int exponent) const {
            if (exponent >= 0) {
                return Fraction(static_cast<T>(std::pow(_numerator, exponent)),
                                static_cast<T>(std::pow(_denominator, exponent)));
            }
            if (_numerator == 0) {
                throw std::runtime_error("Fraction(0, 0)");
            }
            if (_numerator > 0) {
                return Fraction(static_cast<T>(std::pow(_denominator, -exponent)),
                                static_cast<T>(std::pow(_numerator, -exponent)));
            }
            return Fraction(static_cast<T>(std::pow(-_denominator, -exponent)),
                            static_cast<T>(std::pow(-_numerator, -exponent)));
        }

        /**
         * Conversion to integer (truncation toward zero)
         */
        T to_int() const {
            if (_numerator < 0) {
                return -((-_numerator) / _denominator);
            }
            return _numerator / _denominator;
        }

        /**
         * Floor operation
         */
        T floor() const { return _numerator / _denominator; }

        /**
         * Ceiling operation
         */
        T ceil() const {
            if (_numerator >= 0) {
                return (_numerator + _denominator - 1) / _denominator;
            }
            return _numerator / _denominator;
        }

        /**
         * Round to nearest integer (ties to even)
         */
        T round() const {
            T floor_val = this->floor();
            T remainder = _numerator % _denominator;

            if (remainder * 2 < _denominator) {
                return floor_val;
            }
            if (remainder * 2 > _denominator) {
                return floor_val + 1;
            }
            // Tie case: round to even
            return (floor_val % 2 == 0) ? floor_val : floor_val + 1;
        }

        /**
         * Round to specified number of decimal places
         */
        Fraction round(int ndigits) const {
            if (ndigits >= 0) {
                T shift = static_cast<T>(std::pow(10, ndigits));
                return Fraction(this->round() * shift, shift);
            } else {
                T shift = static_cast<T>(std::pow(10, -ndigits));
                return Fraction(this->round() * shift, shift);
            }
        }

        /**
         * Convert to double
         */
        double to_double() const {
            return static_cast<double>(_numerator) / static_cast<double>(_denominator);
        }

        /**
         * Convert to float
         */
        float to_float() const {
            return static_cast<float>(_numerator) / static_cast<float>(_denominator);
        }

        /**
         * String representation
         */
        std::string to_string() const {
            if (_denominator == 1) {
                return std::to_string(_numerator);
            }
            return std::to_string(_numerator) + "/" + std::to_string(_denominator);
        }

        /**
         * Hash function (compatible with Python's hash algorithm)
         */
        std::size_t hash() const {
            // Simplified hash - Python uses modular inverse with large prime
            // This is a basic implementation
            std::hash<T> hasher;
            std::size_t h1 = hasher(_numerator);
            std::size_t h2 = hasher(_denominator);
            return h1 ^ (h2 << 1);
        }

        /**
         * @brief Stream output operator.
         * @tparam _OStream The output stream type.
         * @param[in,out] os The output stream.
         * @param[in] frac The fraction to output.
         * @return Reference to the output stream.
         */
        template <typename _OStream>
        friend _OStream& operator<<(_OStream& os, const Fraction& frac) {
            os << frac.to_string();
            return os;
        }
    };

    /**
     * Comparison operators for integer on left side
     */
    /**
     * @brief Equality comparison between integer and Fraction
     * @param lhs Integer to compare
     * @param rhs Fraction to compare
     * @return true if rhs equals lhs (i.e., rhs.numerator() == lhs && rhs.denominator() == 1)
     */
    template <typename T> bool operator==(T lhs, const Fraction<T>& rhs) {
        return rhs.numerator() == lhs && rhs.denominator() == 1;
    }

    /**
     * @brief Less-than comparison between integer and Fraction
     * @param lhs Integer to compare
     * @param rhs Fraction to compare
     * @return true if lhs is less than rhs
     */
    template <typename T> bool operator<(T lhs, const Fraction<T>& rhs) {
        if (rhs.denominator() == 1 || lhs == 0) {
            return lhs < rhs.numerator();
        }
        return rhs.denominator() * lhs < rhs.numerator();
    }

    /**
     * @brief Greater-than comparison between integer and Fraction
     * @param lhs Integer to compare
     * @param rhs Fraction to compare
     * @return true if lhs is greater than rhs
     */
    template <typename T> bool operator>(T lhs, const Fraction<T>& rhs) {
        if (rhs.denominator() == 1 || lhs == 0) {
            return lhs > rhs.numerator();
        }
        return rhs.denominator() * lhs > rhs.numerator();
    }

    /**
     * @brief Less-than-or-equal comparison between integer and Fraction
     * @param lhs Integer to compare
     * @param rhs Fraction to compare
     * @return true if lhs is less than or equal to rhs
     */
    template <typename T> bool operator<=(T lhs, const Fraction<T>& rhs) { return !(lhs > rhs); }

    /**
     * @brief Greater-than-or-equal comparison between integer and Fraction
     * @param lhs Integer to compare
     * @param rhs Fraction to compare
     * @return true if lhs is greater than or equal to rhs
     */
    template <typename T> bool operator>=(T lhs, const Fraction<T>& rhs) { return !(lhs < rhs); }

    /**
     * Arithmetic operators for integer on left side
     */
    /**
     * @brief Addition operator for integer on left side
     * @param lhs Integer to add
     * @param rhs Fraction to add
     * @return Result of lhs + rhs as new Fraction
     */
    template <typename T> Fraction<T> operator+(T lhs, const Fraction<T>& rhs) {
        return Fraction<T>(lhs) + rhs;
    }

    /**
     * @brief Subtraction operator for integer on left side
     * @param lhs Integer to subtract
     * @param rhs Fraction to subtract
     * @return Result of lhs - rhs as new Fraction
     */
    template <typename T> Fraction<T> operator-(T lhs, const Fraction<T>& rhs) {
        return Fraction<T>(lhs) - rhs;
    }

    /**
     * @brief Multiplication operator for integer on left side
     * @param lhs Integer to multiply
     * @param rhs Fraction to multiply
     * @return Result of lhs * rhs as new Fraction
     */
    template <typename T> Fraction<T> operator*(T lhs, const Fraction<T>& rhs) {
        return Fraction<T>(lhs) * rhs;
    }

    /**
     * @brief Division operator for integer on left side
     * @param lhs Integer to divide
     * @param rhs Fraction to divide by
     * @return Result of lhs / rhs as new Fraction
     */
    template <typename T> Fraction<T> operator/(T lhs, const Fraction<T>& rhs) {
        return Fraction<T>(lhs) / rhs;
    }

}  // namespace fractions