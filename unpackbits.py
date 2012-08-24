#!/usr/bin/python
import sys

skip = int(sys.argv[1])
count = int(sys.argv[2])
sys.stdin.read(skip)

offset = skip

# Look for the first end of scan line?
started = 1
debug = 0

while True:
    r = sys.stdin.read(1)
    offset += 1
    if not r:
        break
    n = ord(r[0])
    if n < 128:
	count = n + 1
	if debug:
		sys.stderr.write(str(offset) + ": COPY "+str(count)+"\n")
	for i in range(count):
		c = sys.stdin.read(1)
		if debug:
			sys.stderr.write(str(offset) + ": --- " + hex(ord(c)) + "\n")
		offset += 1
		if started:
			sys.stdout.write(c)
    elif n == 128:
	if debug:
		sys.stderr.write(str(offset) + ": SKIP\n")
        pass
    elif n < 256:
        count = (256-n) + 1
        r = sys.stdin.read(1)
	offset += 1
	if debug:
		sys.stderr.write(str(offset) + ": RPT "+str(count)+": " + hex(ord(r)) + "\n")
	if started:
		for i in range(count):
			sys.stdout.write(r)
	if not started and ord(r) == 0x00 and count == 8:
		started = 1
		sys.stderr.write(str(offset) + ": First scanline started\n")
    else:
        sys.stderr.write("ERROR- VAL OUT OF RANGE\n")

