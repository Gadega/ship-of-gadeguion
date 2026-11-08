#!/usr/bin/env python3
"""Read horse resource layout metadata from a user-owned O2R; print no asset bytes."""
import struct
import sys
import zipfile

NAME = 'objects/object_horse/gEponaBodyLimb'


def inspect(data):
    if len(data) < 80:
        raise ValueError('resource shorter than 80 bytes')
    endian = '>' if data[0] else '<'
    at = 64

    def read(fmt):
        nonlocal at
        size = struct.calcsize(endian + fmt)
        if at + size > len(data):
            raise ValueError(f'truncated at byte {at}; need {size} bytes')
        result = struct.unpack_from(endian + fmt, data, at)
        at += size
        return result[0] if len(result) == 1 else result

    def name():
        nonlocal at
        if at + 4 > len(data):
            raise ValueError(f'truncated string length at {at}')
        length = struct.unpack_from('<I', data, at)[0]
        at += 4
        if length > 512 or at + length > len(data):
            raise ValueError(f'invalid string length={length} at byte {at - 4}')
        at += length
        return length

    limb_type = read('B')
    segment_type = read('B')
    first_name = name()
    total_vertices = read('H')
    mod_count = read('I')
    print(f'limb_type={limb_type} segment_type={segment_type} '
          f'skin_dlist_name_length={first_name} total_vertices={total_vertices} modifications={mod_count}')
    if mod_count > 1000:
        raise ValueError('modification count is implausibly large; stopping')
    total_decoded_vertices = 0
    total_transforms = 0
    max_index = -1
    for mod in range(mod_count):
        unknown = read('H')
        vertex_count = read('i')
        if vertex_count < 0 or vertex_count > 100000:
            raise ValueError(f'mod={mod}: invalid vertex count={vertex_count}')
        for _ in range(vertex_count):
            max_index = max(max_index, read('H'))
            read('hhbbbB')
        transform_count = read('i')
        if transform_count < 0 or transform_count > 100000:
            raise ValueError(f'mod={mod}: invalid transform count={transform_count}')
        for _ in range(transform_count):
            read('BhhhB')
        total_decoded_vertices += vertex_count
        total_transforms += transform_count
        print(f'mod={mod} unknown={unknown} vertices={vertex_count} transformations={transform_count}')
    second_name = name()
    for _ in range(3):
        read('f')
    for _ in range(3):
        read('H')
    name_lengths = [name() for _ in range(4)]
    for _ in range(3):
        read('h')
    read('B')
    read('B')
    print(f'vertices_in_mods={total_decoded_vertices} max_vertex_index={max_index} '
          f'transforms={total_transforms} second_dlist_name_length={second_name} '
          f'other_name_lengths={name_lengths} parsed_bytes={at} resource_bytes={len(data)}')


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python3 scripts/inspect-ps3-horse-resource.py /path/to/oot.o2r')
    with zipfile.ZipFile(sys.argv[1]) as archive:
        entries = [entry for entry in archive.namelist() if entry.lower() == NAME.lower()]
        if len(entries) != 1:
            raise SystemExit(f'Expected one {NAME}; found {len(entries)}')
        data = archive.read(entries[0])
    print('resource:', NAME, 'type:', int.from_bytes(data[4:8], 'big' if data[0] else 'little'))
    inspect(data)
