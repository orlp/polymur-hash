# PolymurHash

PolymurHash is a 64-bit [universal hash
function](https://en.wikipedia.org/wiki/Universal_hashing) designed for use
in hash tables. It has a couple desirable properties:

 - It is **mathematically proven** to have a statistically low collision rate.
   When initialized with an independently chosen random seed, for any distinct
   pair of inputs `m` and `m'` of up to `n` bytes the probability that `h(m) =
   h(m')` is at most `n * 2^-60.2`. This is known as an almost-universal hash
   function. In fact PolymurHash has a stronger property: it is almost pairwise
   independent. For any two distinct inputs `m` and `m'` the probability they
   hash to the pair of specific 64-bit hash values `(x, y)` is at most `n *
   2^-124.2`.
 
 - It is very fast for short inputs, while being no slouch for longer inputs. On
   an Apple M1 machine it can hash any input <= 49 bytes in 21 cycles, and
   processes 33.3 GiB/sec (11.6 bytes / cycle) for long inputs.
   
 - It is cross-platform, using no extended instruction sets such as
   CLMUL or AES-NI. For good speed it only requires native 64 x 64 -> 128 bit
   multiplication, which almost all 64-bit processors have.

 - It is small in code size and space. Ignoring cross-platform compatibility
   definitions, the hash function and initialization procedure is just over 100
   lines of C code combined. The initialized hash function uses 32 bytes of
   memory to store its parameters, and it uses only a small constant amount of
   stack memory when computing a hash.
   
To my knowledge PolymurHash is the first hash function to have all those
properties. There are already very fast universal hash functions, such as
[CLHASH](https://github.com/lemire/clhash),
[umash](https://github.com/backtrace-labs/umash),
[HalftimeHash](https://github.com/jbapple/HalftimeHash), etc., but they all
require a large amount of space (1KiB+) to store the hash function parameters,
are not optimized for hashing small strings, and/or require specific instruction
sets such as CLMUL. There are also very fast cross-platform hashes such as
[xxHash3](https://github.com/Cyan4973/xxHash),
[komihash](https://github.com/avaneev/komihash) or
[wyhash](https://github.com/wangyi-fudan/wyhash), but they do not come with
proofs of universality. [SipHash](https://en.wikipedia.org/wiki/SipHash) claims
to be cryptographically secure, but is relatively slow, leading people to use
reduced-round variants with unknown cryptanalysis, or to use a fast but insecure
hash altogether.

Needless to say, PolymurHash passes the full [SMHasher
suite](https://github.com/rurban/smhasher/) without any failures[*](https://github.com/rurban/smhasher/issues/114#issuecomment-1587631635). For the proof
of the collision rate, see
[`extras/universality-proof.md`](extras/universality-proof.md).


## How to use it

PolymurHash is provided as a header-only C library `polymur-hash.h`. Simply
include it and you are good to go. First the hash function must have its
`PolymurHashParams` initialized from a seed, for which there are two functions.
PolymurHash uses two 64-bit secrets, but provides a convenient function to
expand a single 64-bit seed to that:

```c
void polymur_init_params(PolymurHashParams* p, uint64_t k, uint64_t s);
void polymur_init_params_from_seed(PolymurHashParams* p, uint64_t seed);
```

The proof of almost universality applies to both, but for the proof of almost
pairwise independence to hold you must provide 128 bits of entropy. Once
initialization is complete, you can compute as many hashes as you want with it:

```c
// Computes the full hash of buf. The tweak is added to the hash before final
// mixing, allowing different outputs much faster than re-seeding. No claims are
// made about the collision probability between hashes with different tweaks.
uint64_t polymur_hash(const uint8_t* buf, size_t len, const PolymurHashParams* p, uint64_t tweak);
```

The tweak allows you to have a different hash function for each hash table
without having to calculate new parameters from another seed. This can prevent
certain worst-case problems when inserting into a second hash table while
iterating over another.

### License

PolymurHash is available under the zlib license, included in `polymur-hash.h`.


## How it works and why it's fast

At its core, PolymurHash is a Carter-Wegman style polynomial hash that treats
the input as a series of coefficients for a polynomial, and then evaluates that
polynomial at a secret key `k` modulo some prime `p`. The result of this
universal hash is then fed into a Murmur-style permutation followed by the
addition of a second secret key `s`. The polynomial part of the hash provides
the universality guarantee, and the Murmur-style bit mixing part provides good
bit avalanching and uniformity over the full 64 bit output. The final addition
of `s` provides pairwise independence and resistance against cryptanalysis by
making the otherwise trivially invertible permutation a lot harder to invert.

Now to make the above fast, a couple tricks are employed. The prime used is
`p = 2^61 - 1`, a Mersenne prime. By expressing any number `x` as `2^61 a + b`
where `b < 2^61`, we notice that mod `p` this is equal to just `a + b`. With
this we can do efficient reduction:

    reduce(x) = (x >> 61) + (x & P611)
    
This allows us to keep the numbers small very efficiently. Furthermore we also
limit `k` during initialization in such a way that we overflow 64/128 bit
numbers less often and thus need to perform the above reduction less often.

We also use a trick similar to the one found in "Polynomial evaluation and
message authentication" by Daniel J. Bernstein, where we forego computing an
exact polynomial `m[0]k + m[1]k^2 + m[2]k^3 + ...` and instead compute any
polynomial that is injective. That is, we're good as long as each input maps to
a distinct polynomial. Then we can use the 'pseudo-dot-product' to compute

    (k + m[0])*(k^2 + m[1]) + (k^3 + m[2])*(k^4 + m[3]) + ...

which allows us to process twice as much data per multiplication. It also
allows us to pre-compute a couple powers of `k` to then use instruction-level
parallelism to further increase throughput. The loop used for large inputs

    m[i] = loadword(buf + 7*i) & ((1 << 56) - 1)
    f =  (f   + m[6]) * k^7
    f += (k   + m[0]) * (k^6 + m[1])
    f += (k^2 + m[2]) * (k^5 + m[3])
    f += (k^3 + m[4]) * (k^4 + m[5])
    f = reduce(f)

processes 49 bytes of input using seven parallel 64-bit additions and binary
ANDs, four parallel 64 x 64 -> 128 bit multiplications, three 128-bit additions
and one 128-bit to 64-bit modular reduction. For small inputs we use custom
injective polynomials that are fast to evaluate, filled with input from
potentially overlapping reads to avoid branches on the input length.


## Resistance against cryptanalysis

PolymurHash has strong collision guarantees for input chosen independently from
its random seed. An interactive attacker however can craft its input based on
earlier seen hashes. PolymurHash is **not** a cryptographically secure
collision-resistant hash if the attacker can see (or worse, request) hash values
at will. This is not a failure of the underlying hash, for example the
well-known Poly1305 hash used to secure TLS suffers from the same problem. It
solves this by hiding its hash values from attackers by adding an encrypted
nonce acting as a one-time-pad.

PolymurHash is not intended to be used in a context where an attacker can see
the hash values. Its main intended use case is as a hash function for
DoS-resistant hash tables. However, a clever attacker might still acquire
*some* information about the hash values by using side-channels such as
hash table iteration order, or timing attacks on collisions.

To protect against this PolymurHash is structured in a way so that it is not
trivial to invert the hash, nor to set up controlled informative experiments
through side-channels. Roughly speaking, every bit of input is first mixed with
the secret key `k` by modular multiplication and addition modulo a prime. This
is a linear process, so to hide the linear structure of the underlying hash we
pass the resulting value through a Murmur-style bit-mixing permutation which is
highly non-linear. Finally, to ensure the permutation is not trivially
invertible, we add the second secret key `s`.

The above process is by no means a strong multi-round cipher and would likely
not hold up to proper cryptanalysis in a standard context. But the underlying
structure is sound (e.g. the ChaCha cipher has the form `mix(secret + input) +
secret` where `mix` is an unkeyed permutation), and I believe that extrapolating
what little information you can get from side-channels to recover `k, s` to
execute a HashDoS attack is difficult.


## No weak keys

Polynomial hashing has one potential issue: weak keys. The multiplicative group
of numbers mod `p = 2^61-1` has subgroups of (much) smaller size. This means
that for some keys `k^(i + d) mod p == k^i mod p` for small constant `d`. In
other words, you can swap the 7-byte block at index `i` with the one at `i + d`
without changing the hash value for such a weak key.

What is the probability you choose such a weak key if you pick a key at random
from the 2^61 - 2 possible keys? If `d` is a divisor of `p - 1`, then there are
`totient(d)` such keys with the above property. And of course, for the attack to
work we need `d <= n / 7`. Here is a small table showing the probabilities:

    Max length of input   Divisors   Weak key probability
    64 bytes              8          2^-56.5
    1 kilobyte            49         2^-50.5
    1 megabyte            811        2^-37.3
    1 gigabyte            3420       2^-26.1
    
These probabilities are very small. Nevertheless, it didn't sit well with me
that the possibility existed of picking a key that has such a flaw. So, the
seeding algorithm for PolymurHash makes sure to select a key that is a
*generator* of the multiplicative group, that is, the only solution to
`k^(i + d) mod p == k^i mod p` is `d = 2^61 - 1`.

In other words, PolymurHash does not suffer from weak keys. As a trade-off our
key space is slightly more limited: in combination with the fact we want `k^7 <
2^60 - 2^56` for efficiency reasons we get a total key space of `totient(2^61 -
2) * (2^60 - 2^56) / (2^61 - 1) ~= 2^57.4`. Additionally, initialization is also
slower than simply selecting a random key, on an Apple M1 it takes ~300 cycles
on average. If you feel the need to seed many different hashes, consider looking
at the `tweak` parameter instead to see if it fits your criteria.


# Acknowledgements

I am standing on the shoulders of giants, and in the well-researched field of
(universal) hash functions there are a lot of them. J. Lawrence Carter, Mark N.
Wegman, Ted Krovetz, Phillip Rogaway, Mikkel Thorup, Daniel J. Bernstein, Daniel
Lemire, Martin Dietzfelbinger, Austin Appleby, many names come to mind. I have
read many publications by them, and borrowed ideas from all of them.
