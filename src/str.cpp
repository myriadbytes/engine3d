#include "str.h"
#include "containers.h"
#include "maths.h"

#include <cstdarg>

void skipWhitespace(Slice<const u8>& input) {
    while (*input.ptr == ' ') {
        input.ptr++;
        input.len --;
    }
}

u8 consumeChar(Slice<const u8>& input) {
    if (!input.len) return '\0'; 

    u8 result = *input.ptr;

    input.ptr++;
    input.len --;

    return result;
}

void outputChar(Slice<u8>& output, u8 c) {
    if (!output.len) return; 

    *output.ptr = c;

    output.ptr++;
    output.len --;
}

template <typename T>
void outputInteger(Slice<u8>& output, T n) {
    // NOTE: The algorithm 'stringifies' the least significant digit first,
    // so we need to store the chars and reverse them before printing.
    // The max value of u64 needs 20 characters to be printed out,
    // so 32 chars in the buffer is plenty.
    SStack<u8, 32> tmp_buffer;

    // NOTE: Save the sign for later, then make the number positive.
    b32 is_negative = (n < 0);
    if (is_negative) {
        n = -n;
    }

    // NOTE: Output all the digits into the temporary buffer.
    // We use a do-while loop in order to handle the case where
    // the user tries to print the number 0.
    do {
        T digit = n % 10;
        tmp_buffer.push('0' + digit);
        n /= 10;
    } while (n > 0);

    // NOTE: Add the sign if the number was negative.
    if (is_negative) {
        tmp_buffer.push('-');
    }

    // Write the digits in the output buffer, in the reverse order.
    while (!tmp_buffer.is_empty()) {
        u8 c = tmp_buffer.pop();
        outputChar(output, c);
    }
}

void outputSize(Slice<u8>& output, usize n) {
    StrView unit_str = StrView(nullptr, 0);

    // NOTE: Figure out the unit to use, then scale the
    // number accordingly.
    if (n > GIGABYTES(1)) {
        n /= GIGABYTES(1);
        unit_str = " GB";
    }
    else if (n > MEGABYTES(1)) {
        n /= MEGABYTES(1);
        unit_str = " MB";
    }
    else if (n > KILOBYTES(1)) {
        n /= KILOBYTES(1);
        unit_str = " KB";
    }
    else {
        unit_str = " BYTES";
    }

    // NOTE: Print the scaled number.
    outputInteger(output, n);

    // NOTE: Then the unit.
    while (unit_str.len) {
        u8 c = consumeChar(unit_str);
        outputChar(output, c);
    }
}

// NOTE: This very cool and simple float-to-string algorithm comes from here :
// https://blog.benoitblanchon.fr/lightweight-float-to-string/
// It trades some features (i.e. unlimited decimal places, custom formatting)
// for simplicity and speed. This version is even simpler, since it only prints
// up to 2 decimal places.
// TODO: The number of digits after the comma should be configurable.
void outputFloat(Slice<u8>& output, f64 x) {
    // NOTE: The algorithm works by splitting the number into 4 parts,
    // like scientific notation :
    // - Sign
    // - Integral part
    // - Decimal part
    // - Exponent
    // Each part is converted to a u32, so there needs to be some normalization
    // happening in case one of the parts exceeds this limit. For example, if
    // the  integral part is bigger than 4 billions, we would divide it by 10
    // and increase the exponent part.

    constexpr u32 DIGITS_AFTER_COMMA = 2;
    constexpr f64 DECIMAL_PART_POW = 1e2;

    // NOTE: Handle negative numbers, NaN, etc.
    // TODO: NaN and Inf.
    if (x < 0) {
        outputChar(output, '-');
        x = -x;
    }

    // NOTE: Normalize the number in case it would be too big (integer part),
    // or too small (decimal part) to extract the parts into u32s, then do
    // exactly that. Since we are running on big computers, I am doing it the
    // easy way using fancy fp functions like pow and log10, instead of the
    // clever but verbose binary exponentiation presented in the original blog
    // post.
    u32 exponent = 0;

    if (x > 1e7 || x < 1e-5) {
        exponent = floor(log10(x));
        x /= pow(10., exponent);
    }

    u32 integral_part = (u32) x;
    f64 remainder = x - (f64) integral_part;
    u32 decimal_part = (u32) (remainder * DECIMAL_PART_POW);

    // NOTE: Round the decimal part.
    if (remainder * 1e2 - (f64)decimal_part > 0.5f) {
        decimal_part++;

        if (decimal_part >= 100) {
            decimal_part = 0;
            integral_part++;
            if (exponent != 0 && integral_part >= 10) {
                exponent++;
                integral_part = 1;
            }
        }
    }

    // NOTE: Print each part, one after the other.
    outputInteger(output, integral_part);
    if (decimal_part) {
        // NOTE: We can't just call outputInteger for the decimal part, because
        // the logic is a little different. We want to output exactly
        // DIGITS_AFTER_COMMA characters, and add zeroes if necessary. Otherwise
        // with two digits after the comma and a decimal part of 2, we would get
        // "16.2" instead of the correct "16.02".
        outputChar(output, '.');

        SStack<u8, 32> decimal_digits;
        for (i32 remaining = DIGITS_AFTER_COMMA; remaining > 0; remaining--) {
            decimal_digits.push('0' + decimal_part % 10);         
            decimal_part /= 10;
        }

        while (!decimal_digits.is_empty()) {
            outputChar(output, decimal_digits.pop());
        }
    }
    if (exponent) {
        outputChar(output, 'e');
        outputInteger(output, exponent);
    }
}

StrView formatStringVAList(Slice<u8> buffer, StrView fmt, va_list args_list) {

    Slice<u8> output = buffer;
    Slice<const u8> input = fmt;

    while (input.len) {
        u8 fchar = consumeChar(input);
        switch (fchar) {
            case '{': {
                skipWhitespace(input);

                // NOTE: Extract the placeholder.
                StrView code = StrView(input.ptr, 0);
                while (true) {
                    u8 c = consumeChar(input);

                    if (c == '}') {
                        break;
                    } else {
                        code.len++;
                    };
                }

                // NOTE: Now, we can compare it.
                if (code == "u32" || code == "u16" || code == "u8") {
                    // NOTE: Variable argument lists in C++ automatically
                    // promote smaller types to ints. So the minimum width
                    // to read is always 4 bytes.
                    u32 val = va_arg(args_list, u32);
                    outputInteger(output, val);
                }
                else if (code == "u64") {
                    u64 val = va_arg(args_list, u64);
                    outputInteger(output, val);
                }
                else if (code == "i32" || code == "i16" || code == "i8") {
                    // NOTE: Same thing as for the u32/u16/u8 case.
                    i32 val = va_arg(args_list, i32);
                    outputInteger(output, val);
                }
                else if (code == "i64") {
                    i64 val = va_arg(args_list, i64);
                    outputInteger(output, val);
                }
                else if (code == "f32" || code == "f64") {
                    // NOTE: C/C++ varargs promote floats to double, so
                    // we only need to care about the double case.
                    f64 val = va_arg(args_list, f64);
                    outputFloat(output, val);
                }
                else if (code == "size") {
                    usize val = va_arg(args_list, usize);
                    outputSize(output, val);
                }
            } break;

            default: {
                outputChar(output, fchar);
            } break;
        }
    }

    return StrView(buffer.ptr, output.ptr - buffer.ptr);
}

StrView formatString(Slice<u8> buffer, StrView fmt, ...) {
    va_list args_list;
    va_start(args_list, fmt);
    StrView result = formatStringVAList(buffer, fmt, args_list);
    va_end(args_list);

    return result;
}
