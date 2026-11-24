"""Estimate remaining route costs for recorded projections on the frozen campaign graph."""
import argparse
from collections import defaultdict
import csv
from functools import lru_cache
import heapq
import math
import mmap
from pathlib import Path
import sys

from extract_rank_replay import Dump, map_symbols


def evaluate(coefficients, t):
    value = 0.
    for c in coefficients:
        value = value*t + c
    return value


class RoadCosts:
    def __init__(self, dump, symbols, target):
        self.dump = dump
        self.curve = symbols['@Ai454e80_Find@24']
        self.line = symbols['@Ai455560_Forward@24']
        bss = symbols['_KingBss'] - 0x68a820
        self.edges = dump.pointers(bss+0x68cf38)
        nodes = dump.pointers(bss+0x68cf00)
        incoming = defaultdict(list)
        for index, node in enumerate(nodes):
            for direction in (0, 1):
                for edge in dump.pointers(node+20+12*direction):
                    endpoints = dump.unpack('II', edge+112)
                    neighbour, = dump.unpack('I', endpoints[1-direction]+16)
                    incoming[neighbour].append((index, self.weight(edge, direction)))
        self.costs = {target: 0.}
        queue = [(0., target)]
        while queue:
            cost, node = heapq.heappop(queue)
            if cost > self.costs[node]:
                continue
            for other, weight in incoming[node]:
                proposed = cost + weight
                if proposed < self.costs.get(other, math.inf):
                    self.costs[other] = proposed
                    heapq.heappush(queue, (proposed, other))

    @lru_cache(maxsize=None)
    def lengths(self, obj):
        dump = self.dump
        vtable, = dump.unpack('I', obj)
        method, = dump.unpack('I', vtable+12)
        if method == self.curve:
            start, end = dump.unpack('II', obj+56)
            result = []
            for curve in range(start, end, 1592):
                degree_count, = dump.unpack('I', curve+1584)
                result.append(dump.unpack(f'{degree_count}d', curve+1392))
            return result
        if method != self.line:
            raise ValueError('Unknown road object')
        start, end = dump.unpack('II', obj+88)
        points = [dump.unpack('3d', p) for p in range(start, end, 24)]
        return [(math.dist(a, b), 0.) for a, b in zip(points, points[1:])]

    def factor(self, obj, direction):
        kind, = self.dump.unpack('I', obj+28)
        flags, = self.dump.unpack('I', obj+8)
        restricted = flags & (64 | (4 if direction else 8))
        return {2: 1.5, 3: 2.}.get(kind, 1.) * (10000. if restricted else 1.)

    def weight(self, edge, direction):
        if self.dump.read(edge+56+16*direction, 1)[0]:
            return self.dump.unpack('d', edge+48+16*direction)[0]
        cost = 0.
        for obj in self.dump.pointers(edge+120):
            length = sum(abs(evaluate(poly, 1.)-evaluate(poly, 0.)) for poly in self.lengths(obj))
            cost += length * self.factor(obj, direction)
        return cost

    def estimate(self, edge_id, obj_index, segment, fraction):
        edge = self.edges[edge_id]
        before = after = 0.
        for j, obj in enumerate(self.dump.pointers(edge+120)):
            for k, poly in enumerate(self.lengths(obj)):
                if (j, k) <= (obj_index, segment):
                    t = fraction if (j, k) == (obj_index, segment) else 1.
                    before += abs(evaluate(poly, t)-evaluate(poly, 0.)) * self.factor(obj, 1)
                if (j, k) >= (obj_index, segment):
                    t = fraction if (j, k) == (obj_index, segment) else 0.
                    after += abs(evaluate(poly, 1.)-evaluate(poly, t)) * self.factor(obj, 0)
        a, b = (self.dump.unpack('I', node+16)[0] for node in self.dump.unpack('II', edge+112))
        if before < .1:
            return self.costs[a]
        if after < .1:
            return self.costs[b]
        return min(before+self.costs.get(a, math.inf), after+self.costs.get(b, math.inf))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('session', type=Path)
    parser.add_argument('projections', type=Path)
    parser.add_argument('target', type=int, help='destination node in the dump')
    parser.add_argument('first_frame', type=int)
    parser.add_argument('last_frame', type=int)
    args = parser.parse_args()
    with (args.session/'crash.dmp').open('rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        graph = RoadCosts(Dump(data), map_symbols(args.session), args.target)
        writer = csv.writer(sys.stdout, lineterminator='\n')
        writer.writerow(('frame','probe','room','road','object','segment','fraction','remaining_cost_estimate'))
        for line in args.projections.read_text(encoding='utf-8-sig').splitlines():
            if line.startswith('projection '):
                fields = line.split()
                frame, probe, room, road, obj, seg = map(int, fields[1:7])
                if args.first_frame <= frame <= args.last_frame:
                    t = float(fields[7])
                    writer.writerow((frame,probe,room,road,obj,seg,t,round(graph.estimate(road,obj,seg,t),2)))
