"""Extract numeric projection inputs from a local full x86 dump and decoded driving session."""
import argparse
import bisect
import collections
import json
import mmap
from pathlib import Path
import re
import struct


class Dump:
    def __init__(self, data):
        self.data = data
        count, directory = struct.unpack_from('<II', data, 8)
        for i in range(count):
            kind, _, location = struct.unpack_from('<III', data, directory + 12*i)
            if kind == 9:  # Memory64ListStream
                break
        else:
            raise ValueError('Full memory stream missing')
        size, offset = struct.unpack_from('<QQ', data, location)
        ranges = []
        for i in range(size):
            start, length = struct.unpack_from('<QQ', data, location + 16 + 16*i)
            ranges.append((start, start + length, offset))
            offset += length
        self.ranges = sorted(ranges)
        self.starts = [r[0] for r in self.ranges]

    def read(self, address, length):
        start, end, offset = self.ranges[bisect.bisect_right(self.starts, address)-1]
        if address < start or address + length > end:
            raise ValueError(f'Missing dump range at {address:#x}')
        offset += address - start
        return self.data[offset:offset+length]

    def unpack(self, fmt, address):
        fmt = '<' + fmt
        return struct.unpack(fmt, self.read(address, struct.calcsize(fmt)))

    def pointers(self, address):
        first, end = self.unpack('II', address)
        if end < first or (end - first) % 4:
            raise ValueError('Invalid pointer list')
        return self.unpack(f'{(end-first)//4}I', first) if first != end else ()

    def name(self, address):
        start, end = self.unpack('II', address)
        return self.read(start, end-start).rstrip(b'\0').decode('cp1251') if start != end else ''


def write_object(out, dump, address, methods):
    vtable, = dump.unpack('I', address)
    method, = dump.unpack('I', vtable+12)
    curve = methods[method]
    first, end = dump.unpack('II', address + (56 if curve else 88))
    stride = 1592 if curve else 24
    if end < first or (end-first) % stride:
        raise ValueError('Invalid road geometry list')
    out.write(struct.pack('<ii', curve, (end-first)//stride))
    if curve:
        for p in range(first, end, stride):
            count, = dump.unpack('I', p+576)
            if not 1 <= count <= 12:
                raise ValueError('Unexpected polynomial degree')
            out.write(struct.pack('<i', count))
            out.write(dump.read(p, count*24))
            out.write(dump.read(p+584, 32))
    else:
        out.write(dump.read(address+56, 32))
        out.write(dump.read(first, end-first))


def map_symbols(session):
    return dict((name, int(address, 16)) for name, address in re.findall(
        r'^\s*\w{4}:\w{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+', (session/'king.map').read_text(), re.M))


def extract(session, dump, times, radius, out):
    symbols = map_symbols(session)
    bss = symbols['_KingBss'] - 0x68a820
    methods = {symbols['@Ai455560_Forward@24']: False, symbols['@Ai454e80_Find@24']: True}
    rooms = {dump.name(r): (i, r) for i, r in enumerate(dump.pointers(bss+0x68d0d8))}
    offsets = dump.unpack('12d', bss+0x68cc28)
    probes = [(0., 0., 0.)] + [offsets[i:i+3] for i in range(0, 12, 3)]
    samples = collections.defaultdict(list)
    room = None
    with (session/'decoded/events.jsonl').open(encoding='utf-8') as events:
        for line in events:
            row = json.loads(line)
            if row['kind'] == 'event' and row['name'].startswith('player_room:'):
                room = row['name'].split(':', 1)[1]
            if row['kind'] != 'player' or not any(abs(row['seconds']-t) <= radius for t in times):
                continue
            if room not in rooms:
                raise ValueError(f'No matching road registry for room {room}')
            matrix = row['matrix']
            for i, offset in enumerate(probes):
                point = [matrix[9+j] + sum(offset[k]*matrix[3*k+j] for k in range(3)) for j in range(3)]
                samples[room].append((row['frame'], i, *point))
    if not samples:
        raise ValueError('No player samples in the requested interval')
    out.write(b'RPR1' + struct.pack('<i', len(samples)))
    for name, rows in samples.items():
        room_id, record = rooms[name]
        edges = dump.pointers(record+40)
        out.write(struct.pack('<ii', room_id, len(edges)))
        for edge in edges:
            edge_id, = dump.unpack('I', edge+12)
            objects = dump.pointers(edge+120)
            out.write(struct.pack('<ii', edge_id, len(objects)))
            for obj in objects:
                write_object(out, dump, obj, methods)
        out.write(struct.pack('<i', len(rows)))
        for row in rows:
            out.write(struct.pack('<ii3d', *row))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('seconds', type=float, nargs='+', help='centres on the decoded telemetry clock')
    parser.add_argument('--radius', type=float, default=8.)
    args = parser.parse_args()
    with (args.session/'crash.dmp').open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        with args.output.open('xb') as output:
            try:
                extract(args.session, Dump(data), args.seconds, args.radius, output)
            except BaseException:
                output.close()
                args.output.unlink()
                raise
