# Proof of almost universality

The proof below assumes that our secret key `k` was chosen uniformly at random
from all possible `K`, which in [the readme](../README.md#no-weak-keys) is
described to be roughly 2^57.4 possible keys. Furthermore, it assumes this
random choice is independent from our input.

The first step in PolymurHash is to injectively map the input string onto a
series of 56-bit (7-byte) integers `m[0]`, `m[1]`, `m[2]`, etc. How this is done
is rather specific to the size of the input for maximum speed, with some input
bits sometimes occurring multiple times in the `m`s due to overlapping reads,
but ultimately it is always possible to unambiguously reconstruct the original
input from `m[0]`, `m[1]`, etc, given the length.

We then injectively map these `m[i]` to a polynomial `f_m(k)` in variable `k`.
That is, if two polynomials `f_m = f_m'` are equal, we find that their
corresponding original hash function inputs `m, m'` must also be equal. We split
the input into blocks of 49 bytes, with only the last block having a non-zero
length <= 49. In total we have `1 + floor((n - 1) / 49)` blocks.

All except the last block are encoded in the same way. Our polynomial `f`
is initially `0` and we build it up in block-Horner form. For each full block
consisting of seven `m[i]` we update `f` as such:

    f = k^7 * (f + m[6])
    f += (k   + m[0]) * (k^6 + m[1])
    f += (k^2 + m[2]) * (k^5 + m[3])
    f += (k^3 + m[4]) * (k^4 + m[5])
    
The result is that we multiply all existing terms in `f` by `k^7` before adding


    (m[6] + 3)*k^7 + m[0]*k^6 + m[2]*k^5 + m[4]*k^4 + m[5]*k^3 + m[3]*k^2 + m[1]*k
    + (m[0]*m[1] + m[2]*m[3] + m[4]*m[5])

except we do it using just four multiplications. This process is clearly
injective, as we can reverse it by simply reading off `m[0]` through `m[6]` from
the top 7 exponents, before subtracting the above expression from the polynomial
to continue decoding. Later additions to the polynomial do not interfere
with this as the very next step shifts all exponents up by `7`. Finally,
the top exponent is always non-zero, as `m[6] < 2^56`, so the total number of
blocks in the polynomial is always unambiguous.

To finish off the process before handling the final block we multiply `f` by
`k^14` to shift existing terms out of harms way. Then we add, depending on the
size, a different injective polynomial of maximum degree < 14 to encode the
final block. These polynomials are:

    length 1..7
    f += (k + m[0]) * (k^2 + l)

    k^3 + m[0]*k^2 + l*k + l*m[0]

    length 8..21
    f += (k^2 + m[0]) * (k^7 + m[1])
    f += (k   + m[2]) * (k^3 + l)

    k^9 + m[0]*k^7 + k^4 + m[2]*k^3 + m[1]*k^2 + l*k + (l*m[2] + m[0]*m[1])
    
    length 22..49
    t  = (k^2 + m[0]) * (k^7 + m[1])
    f += (k   + m[2]) * (k^3 + l)
    f += (k^2 + m[3]) * (k^7 + m[4])
    f += (t   + m[5]) * (k^4 + m[6])

    k^13 + m[0]*k^11 + (m[6] + 1)*k^9 + (m[0]*m[6] + m[3])*k^7 + m[1]*k^6 +
        (m[0]*m[1] + m[5] + 1)*k^4 + m[2]*k^3 + (m[1]*m[6] + m[4])*k^2 + l*k +
        (l*m[2] + m[0]*m[1]*m[6] + m[3]*m[4] + m[5]*m[6])
    
Now that last polynomial might not look injective, but it is if you decode it
in stages: first read off `m[0]`, `m[6]`, `m[1]`, `m[2]` directly, after
which you can subtract them in the other terms, e.g. `m[0]*m[6] + m[3]` is now
decodable, etc.

Another important thing of note here is that `l`, the length of the final block
can always be unambiguously and directly read off the coefficient of `k^1`.
This, plus the fact that the number of blocks is also unambiguous makes the
total length of the input string unambigously decodable.

### Collision bound

Now that we have shown PolymurHash constructs an injective polynomial `f_m(k)`
from our input, we can bound our collision probability. We evaluate our
polynomial using our secret key `k`, modulo `p = 2^61 - 1`, a prime, giving our
hash output `PH(m) = f_m(k) mod p`. Note that due to the injectivity of the
polynomial we do not introduce any extraneous collisions: if two polynomials are
equal, then so must be their corresponding messages. So any collisions are due
to the evaluation at `k mod p`.

For any two distinct `m, m'` of at most `n` bytes chosen independently from our
secret key `k` the probability that `PH(m) = PH(m')` is equivalent to asking
`Pr[PH(m) - PH(m') = 0]`. But `x = 0` implies `x mod p = 0` for any `p`,
thus if we can bound `Pr[(PH(m) - PH(m')) mod p = 0]` we also bound the original
collision probability. Since `(a mod p - b mod p) mod p = (a - b) mod p`
our collision probability is upper bounded by

    Pr[(f_m(k) - f_m'(k)) mod p = 0]

Since `f_m` and `f_m'` are both polynomials in `k`, their difference is as well.
This polynomial is non-zero, as when `m` and `m'` have the same length then the
polynomial differs in the corresponding block where `m` and `m'` differ, and if
they have different lengths then they either differ in the topmost exponent
encoding the number of blocks, or in the coefficient of `k` encoding the final
block length.

Since the polynomial `f_m(k) - f_m'(k)` is non-zero, we can ask how many roots it
has. Over the finite field mod `p` it is well known a polynomial has at most the
same number of roots as its maximum degree. The maximum degree of our polynomial
in question is `14 + n / 7`. Thus it has at most that many roots when reduced
mod `p`.

But `m` and `m'` were chosen independently from our secret key `k`. So the
probability that the polynomial `f_m(k) - f_m'(k)` happens to have `k` as its
root mod `p` is at most `(14 + n/7) / |K|` where `K` is the set of all possible
keys. Ignoring the negligible constant 14, and given that we have roughly
`2^57.4` possible keys, this gives us an overall collision bound of

    Pr[PH(m) = PH(m')] <= n/7 * 2^-57.4
    Pr[PH(m) = PH(m')] <= n * 2^-60.2

In reality PolymurHash still performs a permutation after the polynomial
evaluation (the mur in polymur) and adds `s` mod 2^64, but both operations are
invertible and never introduce collisions, so this does not affect our above
argument on the collision probability.

## Proof of almost pairwise independence

The proof below assumes that in addition to `k` being randomly chosen that `s`
is chosen uniformly at random from all 64-bit integers. Furthermore, it assumes
this random choice is independent from our input.

This proof is almost identical to the one above, but takes into account the full
hash. Our full hash is `H(m) = (mix(PH(m)) + s) mod 2^64`, where `s < 2^64` is
another independently chosen secret key and `mix` is a 64-bit permutation
independent from `k, s`. Let `imix` be the inverse of that permutation.

This time we need to prove that for any `m != m'` independently chosen from
`s, k`, as well as any hash outcomes `x, y` that

    Pr[H(m) = x  &&  H(m') = y]

is a small bounded quantity. Let use rewrite some terms:

    H(m) = x   <=>   mix(PH(m)) = (x - s) % 2^64   <=>   PH(m) = imix((x - s) % 2^64)

For brevity write `ixs = imix((x - s) % 2^64)` and similarly for `iys`. Then
our probability can be rewritten as

    Pr[H(m) = x  &&  H(m') = y] =
    Pr[H(m) - H(m') = x - y  &&  H(m') = y] =
    Pr[PH(m) - PH(m') - ixs + iys = 0  &&  mix(PH(m')) = (y - s) % 2^64] = 
    Pr[PH(m) - PH(m') - ixs + iys = 0  &&  s = (mix(PH(m')) - y) % 2^64]

Using the same trick as before we find once again that mod `p` the left hand
equation, irrespective of `s`, is a non-zero polynomial with maximum degree
`n/7`. Thus it has at most `n/7` roots mod p. The right hand equation tells us
that given a certain value of `k` there is a unique solution for `s`. Thus in
total we have at most `n/7` solutions that solve both equations at once. But `k,
s` were picked uniformly at random from a total key space of `2^57.4 * 2^64`
values, thus giving us our bound

    Pr[H(m) = x  &&  H(m') = y] <= n/7 * 2^-121.4
    Pr[H(m) = x  &&  H(m') = y] <= n * 2^-124.2

Of course for the above proofs to be valid, our hashing algorithm needs to
faithfully execute the above. While I don't have a formal verification for
PolymurHash, at the very least I have a simulation in
[`bounds-check.py`](bounds-check.py) that proves that even in the worst case
we appropriately reduce `mod p` when necessary and that no overflows occur.

