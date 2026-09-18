import teptris

doc = teptris.loads("a = 1\nb = [1, {c = \"x\"}]\n[t]\nd = 1979-05-27T07:32:00Z\n")
s = teptris.dumps(doc)
assert "1979-05-27" in s and "x" in s and "a = 1" in s
print("wheel self-test OK")
