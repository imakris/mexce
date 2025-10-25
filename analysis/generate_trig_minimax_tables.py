#!/usr/bin/env python3
"""Generate Chebyshev-based minimax tables for the mexce trig polynomials."""

from __future__ import annotations

import argparse
import ctypes
import sys
from dataclasses import dataclass
from typing import Iterable, List, Sequence, Tuple

import mpmath as mp

# Ensure high precision during coefficient generation.
mp.mp.dps = 160

# Lazy-load libc for strtold so that we can serialise coefficients as extended
# precision values.  The tables in the mexce runtime are stored as 80-bit floats
# split into 64-bit mantissas and 16-bit exponent/sign words.
_libc = ctypes.CDLL(None)
_strtold = _libc.strtold
_strtold.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_char_p)]
_strtold.restype = ctypes.c_longdouble

PI_SQUARED = mp.pi ** 2


@dataclass
class Polynomial:
    """Holds the power-basis coefficients for a minimax approximation."""

    degree: int
    coeffs: Sequence[mp.mpf]

    @property
    def reversed_coeffs(self) -> Iterable[mp.mpf]:
        """Return coefficients ordered from highest to lowest degree."""

        return reversed(self.coeffs)

    def to_uint64_pairs(self) -> List[Tuple[int, int]]:
        """Serialise coefficients into (mantissa, exponent/sign) tuples."""

        pairs: List[Tuple[int, int]] = []
        for coeff in self.reversed_coeffs:
            # Emit a long double via strtold to avoid rounding to binary64 first.
            as_text = mp.nstr(coeff, 70)
            c_str = ctypes.c_char_p(as_text.encode("ascii"))
            end_ptr = ctypes.c_char_p()
            long_double = _strtold(c_str, ctypes.byref(end_ptr))
            raw = ctypes.string_at(
                ctypes.byref(ctypes.c_longdouble(long_double)),
                ctypes.sizeof(ctypes.c_longdouble),
            )
            mantissa = int.from_bytes(raw[:8], "little")
            exp_sign = int.from_bytes(raw[8:16], "little") & 0xFFFF
            pairs.append((mantissa, exp_sign))
        return pairs


def chebyshev_minimax(degree: int) -> Polynomial:
    """Compute Chebyshev-interpolated minimax coefficients for cos(x)."""

    points = degree + 1
    xs = []
    ys = []
    for k in range(points):
        node = mp.cos(mp.pi * (2 * k + 1) / (2 * points))
        y = 0.5 * PI_SQUARED * (1 + node)
        xs.append(y)
        ys.append(mp.cos(mp.sqrt(y)))

    system = mp.matrix(points)
    rhs = mp.matrix(points, 1)
    for i, value in enumerate(xs):
        power = mp.mpf("1")
        for j in range(points):
            system[i, j] = power
            power *= value
        rhs[i] = ys[i]

    coeffs = mp.lu_solve(system, rhs)
    return Polynomial(degree=degree, coeffs=[coeffs[i] for i in range(points)])


def generate_polynomials(min_degree: int, max_degree: int) -> List[Polynomial]:
    return [chebyshev_minimax(degree) for degree in range(min_degree, max_degree + 1)]


def render_tables(polynomials: Sequence[Polynomial]) -> str:
    lines: List[str] = []
    lines.append("static const uint64_t mexce_trig_minimax[%d][32] = {" % len(polynomials))
    for poly in polynomials:
        lines.append("    { // degree %d" % poly.degree)
        pairs = poly.to_uint64_pairs()
        padded = list(pairs)
        while len(padded) < 16:
            padded.append((0, 0))
        for mantissa, exp_sign in padded:
            lines.append(
                "        0x%016xULL, 0x%016xULL," % (mantissa, exp_sign)
            )
        lines.append("    },")
    lines.append("};")
    counts = ", ".join(str(poly.degree + 1) for poly in polynomials)
    lines.append(
        "static const uint8_t mexce_trig_minimax_terms[%d] = { %s };"
        % (len(polynomials), counts)
    )
    return "\n".join(lines)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--min-degree", type=int, default=5, help="lowest polynomial degree"
    )
    parser.add_argument(
        "--max-degree", type=int, default=15, help="highest polynomial degree"
    )
    args = parser.parse_args(argv)

    polynomials = generate_polynomials(args.min_degree, args.max_degree)
    sys.stdout.write(render_tables(polynomials))
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
