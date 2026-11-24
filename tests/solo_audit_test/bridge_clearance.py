"""Compare the archived ZIL trajectory with the two static hit trees of room_cb08."""
import argparse
import hashlib
import itertools
import json
import struct
from pathlib import Path


def checked_file(path, digest):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f'Unexpected resource version: {path}')
    return data


def hit_geometry(data):
    def word(p):
        return struct.unpack_from('<I', data, p)[0]

    def points(p, count):
        return list(struct.iter_unpack('<3f', data[p:p + 12*count]))

    shapes = []

    def node(p, group):
        kind = word(p + 32)
        if kind in (5, 9, 10):
            end = p + (88 if kind == 5 else 72)
            while word(end) == 333:
                end = node(end + 4, group)
                if word(end) != 555:
                    raise ValueError(f'Invalid child terminator at {end}')
                end += 4
                while word(end) == 444:
                    end += 4
            return end
        if kind == 23:
            start = p + 48 + 4*word(p + 44)
            end = start + 4
            for _ in range(word(start)):
                count = word(end)
                shapes.append((group, points(end + 4, count)))
                end += 4 + 12*count
            return end
        if kind == 20:
            count = word(p + 52)
            start = p + 68 + 4*word(p + 64)
            chain = points(start, count)
            shapes.extend((group, [a, b]) for a, b in zip(chain, chain[1:]))
            return start + 12*count
        raise ValueError(f'Unexpected hit node {kind} at {p}')

    for group in ('hit_road_cb08', 'hit_obj_cb08'):
        p = data.index(struct.pack('<I', 333) + group.encode() + b'\0') + 4
        if word(node(p, group)) != 555:
            raise ValueError(f'Invalid group terminator: {group}')
    return shapes


def bound(points):
    return [(min(axis), max(axis)) for axis in zip(*points)]


def zil_corners(tech):
    base = tech.index(b'ZilRBoothSpace\0') - 2056
    points = []
    for count_at, first in [(776, 784), (780, 1024), (1300, 1304)]:
        count, = struct.unpack_from('<I', tech, base + count_at)
        points.extend(struct.unpack_from('<3f', tech, base+first+12*i) for i in range(count))
    # Car_AddMass averages these two centres according to chassis and load mass.
    centres = [struct.unpack_from('<3f', tech, base + i) for i in (1904, 192)]
    limits = bound([tuple(a-b for a, b in zip(p, c)) for p in points for c in centres])
    return list(itertools.product(*limits))


def slab(polygon, axis, lo, hi):
    for boundary, sign in [(lo, 1), (hi, -1)]:
        clipped = []
        for a, b in zip(polygon[-1:] + polygon[:-1], polygon):
            da, db = sign*(a[axis]-boundary), sign*(b[axis]-boundary)
            if (da < 0) != (db < 0):
                clipped.append(tuple(x + da/(da-db)*(y-x) for x, y in zip(a, b)))
            if db >= 0:
                clipped.append(b)
        polygon = clipped
    return polygon


def players(path, start, end):
    room = ''
    with path.open(encoding='utf-8') as f:
        for line in f:
            row = json.loads(line)
            if row['seconds'] > end:
                break
            if row.get('name', '').startswith('player_room:'):
                room = row['name'].split(':', 1)[1]
            if room == 'room_cb08' and row['kind'] == 'player' and row['seconds'] >= start:
                yield row


def incident(shapes, corners, rows):
    vertices = []
    for row in rows:
        m = row['matrix']
        for q in corners:
            vertices.append(tuple(m[9+j] + sum(q[k]*m[3*k+j] for k in range(3)) for j in range(3)))
    x, y, z = bound(vertices)
    levels, separations = {}, []
    for group, polygon in shapes:
        section = slab(polygon, 1, *y)
        footprint = slab(section, 0, *x)
        if footprint:
            levels.setdefault(group, set()).add(tuple(round(v, 4) for v in bound(footprint)[2]))
        # Exclude the supporting road at Z=4. Include any obstacle rising above it.
        side = slab(section, 2, 4.0001, z[1])
        if side:
            left, right = bound(side)[0]
            separations.append(max(0, left-x[1], x[0]-right))
    return dict(samples=len(rows), qpc_seconds=[rows[0]['seconds'], rows[-1]['seconds']],
                enclosing_world_xyz=[x, y, z], surface_z_within_xy={k: sorted(v) for k,v in levels.items()},
                nearest_lateral_gap_above_road=min(separations))


def cross(rows, y):
    for a, b in zip(rows, rows[1:]):
        xa, ya, za = a['matrix'][9:]
        xb, yb, zb = b['matrix'][9:]
        if ya != yb and min(ya, yb) <= y <= max(ya, yb):
            t = (y-ya)/(yb-ya)
            return [round(xa+t*(xb-xa), 3), round(za+t*(zb-za), 3)]
    raise ValueError(f'Trajectory did not cross {y}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resources', type=Path, help='Game copy containing ENV and vehicle.tech')
    parser.add_argument('failed', type=Path, help='Decoded events.jsonl from the incident')
    parser.add_argument('successful', type=Path, help='Decoded events.jsonl from the eight passes')
    args = parser.parse_args()
    sector = checked_file(args.resources/'ENV/cb.b3d', '534e98736e1be8b113e1bc0de3bb3831580e4067b7083078241bacbae9f899b0')
    tech = checked_file(args.resources/'vehicle.tech', '65dd8cc29ad141e389d0c516a8765055f201cceebbdaa86082d48ab5df4e1edc')
    fail = list(players(args.failed, 235.9, 237.5))
    result = incident(hit_geometry(sector), zil_corners(tech), fail)
    result['inputs'] = dict(sector_sha256=hashlib.sha256(sector).hexdigest(),
                            vehicle_tech_sha256=hashlib.sha256(tech).hexdigest())
    for name, path in [('failed_events_sha256', args.failed), ('successful_events_sha256', args.successful)]:
        with path.open('rb') as stream:
            result['inputs'][name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    ys = [2142.251, 2132.78]
    result['crossing_y'] = ys
    result['incident_xz'] = [cross(fail, y) for y in ys]
    result['passes'] = []
    visits = []
    for row in players(args.successful, 0, float('inf')):
        x, y, z = row['matrix'][9:]
        if abs(x-1568.25) <= 25 and abs(y-2137) <= 22 and abs(z-4.95) <= 1.5:
            if not visits or row['seconds'] - visits[-1][-1]['seconds'] > .5:
                visits.append([])
            visits[-1].append(row)
    for rows in visits:
        positions = [row['matrix'][10] for row in rows]
        if min(positions) > min(ys) or max(positions) < max(ys):
            continue
        result['passes'].append(dict(seconds=round(rows[0]['seconds'], 3), southbound=rows[-1]['matrix'][10] < rows[0]['matrix'][10],
                                     xz=[cross(rows, y) for y in ys]))
    print(json.dumps(result, indent=2))
