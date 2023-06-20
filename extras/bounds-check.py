import math

P611 = 2**61 - 1

def check(n, lim=2**64):
    if n >= lim:
        raise ValueError(f"overflow {n} >= lim, log2(n) = {math.log2(n)}, log2(lim) = {math.log2(lim)}")
    return n

def reduce(n):
    return n // 2**61 + n % 2**61

# Maximum value reduce(k) can reach for any k <= n.
def maxreduce(n):
    if n < 2**61: return n
    if n % 2**61 == P611: return (n >> 61) + P611
    return ((n >> 61) - 1) + P611

def u64add(*p):
    return check(sum(p))

def u128add(*p):
    return check(sum(p), 2**128)

def u128mul(a, b):
    return check(a * b, 2**128)

def u64reduce(x):
    return check(maxreduce(x))

# Assume maximal values for each parameter
k = u64reduce(u64reduce(2**64 - 1)) 
k2 = u64reduce(u64reduce(u128mul(k, k)))
k7 = 2**60 - 2**56 - 1
m = 2**56 - 1
l = 49
    
def test_1_7():
    s = u128mul(u64add(k, m), u64add(k2, l))
    return u64reduce(s)

def test_8_21():
    k3 = u64reduce(u128mul(k, k2))
    t0 = u128mul(u64add(k2, m), u64add(k7, m))
    t1 = u128mul(u64add(k, m), u64add(k3, l))
    s = u128add(t0, t1)
    return u64reduce(s)

def test_22_49():
    k3 = u64reduce(u128mul(k, k2))
    k4 = u64reduce(u128mul(k2, k2))
    t0 = u64reduce(u128mul(k2 + m, k7 + m))
    t1 =           u128mul(k  + m, k3 + l)
    t2 =           u128mul(k2 + m, k7 + m)
    t3 =           u128mul(t0 + m, k4 + m)
    s = u128add(t1, t2, t3)
    return u64reduce(s)
    
def test_large():
    k3 = u64reduce(u64reduce(u128mul(k, k2)))
    k4 = u64reduce(u64reduce(u128mul(k2, k2)))
    k5 = u64reduce(u64reduce(u128mul(k, k4))) 
    k6 = u64reduce(u64reduce(u128mul(k2, k4)))
    
    max_h_invariant = 2**64 - m - 1
    t0 = u128mul(u64add(k, m), u64add(k6, m))
    t1 = u128mul(u64add(k3, m), u64add(k4, m))
    t2 = u128mul(u64add(k5, m), u64add(k2, m))
    t3 = u128mul(u64add(max_h_invariant, m), k7)
    s = u128add(t0, t1, t2, t3)
    h = check(u64reduce(s), max_h_invariant + 1)
    
    k14 = u64reduce(u128mul(k7, k7))
    hk14 = u64reduce(u128mul(u64reduce(h), k14))
    return u64reduce(hk14)

max17 = test_1_7()
max821 = test_8_21()
max2249 = test_22_49()
maxlarge = test_large()

check(u64add(maxlarge, max17))
check(u64add(maxlarge, max821))
check(u64add(maxlarge, max2249))
    


# Also ensure our input loading scheme covers all lengths properly.
def load(i, method):
    if method == "mask":
        return range(i, i + 7)
    elif method == "shift":
        return range(i + 1, i + 8)
    else:
        raise RuntimeError("unknown method")

def covers_8_21(l):
    ranges = [load(0, "mask"), load((l-7)//2, "mask"), load(l - 8, "shift")]
    return set.union(*(set(r) for r in ranges)) == set(range(l))

def covers_22_49(l):
    ranges = [load(0, "mask"), load((l-7)//2, "mask"), load(l - 8, "shift")]
    ranges += [load(7, "mask"), load(14, "mask"), load(l - 21, "mask"), load(l - 14, "mask")]
    return set.union(*(set(r) for r in ranges)) == set(range(l))

for l in range(8, 21+1): assert covers_8_21(l)
for l in range(22, 49+1): assert covers_22_49(l)

