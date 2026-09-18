import teptris

doc = teptris.loads("a = 1\nb = [1, {c = \"x\"}]\n[t]\nd = 1979-05-27T07:32:00Z\n")
out = teptris.dumps(doc)
assert "1979-05-27" in out and "x" in out and "a = 1" in out
print("musl binding OK")
