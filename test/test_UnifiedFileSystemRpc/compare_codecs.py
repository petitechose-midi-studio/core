"""Run both compiled codecs over identical generated wire frames; no source rewriting."""
import argparse
import hashlib
import itertools
import json
import random
import struct
import subprocess

p = argparse.ArgumentParser()
p.add_argument('cpp')
p.add_argument('rust')
p.add_argument('--output', required=True)
args = p.parse_args()

def wire(op=6, state=0, error=0, nonce=1, identity=0, delay=10000, body=b'\x07\0\0\0'):
    return struct.pack('<BBBBHHIIII', 0xfc if state == 0 else 0xfd, 4, op, state, 7,
                       error, nonce, identity, delay, len(body)) + body

# Acceptance and rejection fixtures written independently of either codec.
known = [(wire(), True), (wire(op=0, nonce=0, delay=0, body=b''), True),
         (wire(op=13, identity=8, delay=0, body=b''), True),
         (wire(state=2, identity=8, delay=5, body=b''), True),
         (wire(state=1, identity=8, delay=0), True),
         (wire(state=3, error=7, delay=0, body=b''), True),
         (wire(state=4, error=15, identity=8, delay=0, body=b''), True),
         (wire(nonce=0), False), (wire(delay=10001), False),
         (wire(op=13), False), (wire(state=2, identity=8, delay=5), False),
         (wire(state=3, error=0, body=b'', delay=0), False),
         (wire(op=3, state=2, identity=8, body=b'', delay=5), False),
         (wire(state=3, error=7, nonce=0, identity=8, delay=0, body=b''), False)]
known.append((wire()[:1] + b'\x02' + wire()[2:], False))
known.append((wire()[:1] + b'\x03' + wire()[2:], False))
details = b'\0\1\1' + bytes(range(32))
for op in [11, 12, 13, 14]:
    known.append((wire(op=op, state=3, error=8, identity=8, delay=0, body=details), True))
    known.append((wire(op=op, state=3, error=8, identity=8, delay=0, body=details[:-1]), False))
    known.append((wire(op=op, state=3, error=8, identity=8, delay=0, body=b'\0\1\0' + bytes(range(32))), False))
known.append((wire(state=3, error=8, identity=8, delay=0, body=details), False))
corpus = [x[0] for x in known]
for seed, _ in known[:7] + [(wire(op=11, state=3, error=8, identity=8, delay=0, body=details), True)]:
    corpus.extend(seed[:i] for i in range(len(seed)))
    corpus.append(seed + b'\0')
    for position in range(len(seed)):
        for byte in range(256):
            corpus.append(seed[:position] + bytes([byte]) + seed[position+1:])
for op, state, error, nonce, identity, delay in itertools.product(
        range(15), range(5), [0, 7, 15, 18, 19, 20, 21], [0, 1], [0, 8], [0, 5, 10000, 10001]):
    corpus.append(wire(op, state, error, nonce, identity, delay, b''))
rng = random.Random(9042026)
corpus.extend(rng.randbytes(rng.randrange(120)) for _ in range(5000))
corpus += [wire(op=3, state=1, nonce=0, delay=0, body=b'a'*size) for size in [32511, 32512, 32513]]
encoded = b''.join(struct.pack('<I', len(frame)) + frame for frame in corpus)
outputs = [subprocess.run([args.cpp, '--oracle'], input=encoded, capture_output=True, check=True).stdout,
           subprocess.run([args.rust], input=encoded, capture_output=True, check=True).stdout]
assert len(outputs[0]) == len(corpus) == len(outputs[1]), [len(x) for x in outputs]
assert outputs[0] == outputs[1], 'C++/Rust acceptance mismatch'
for i, (_, valid) in enumerate(known):
    assert outputs[0][i] == valid, (i, valid)
assert outputs[0][-3:] == b'\1\1\0'
result = {'frames': len(corpus), 'accepted': sum(outputs[0]), 'rejected': len(corpus)-sum(outputs[0]),
          'corpus_sha256': hashlib.sha256(encoded).hexdigest(),
          'acceptance_sha256': hashlib.sha256(outputs[0]).hexdigest(),
          'known_fixtures': len(known), 'round_trip_checked_inside_both_oracles': True}
with open(args.output, 'w') as f:
    json.dump(result, f, indent=2)
print(json.dumps(result))
