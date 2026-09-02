# Object_Connect level schema

The runtime accepts schema version `1` only. `catalog.json` is the entry point;
all paths in it are relative to the catalog file. Tile matrices are row-major and
must be exactly 45 rows by 80 columns. Tile ID `0` always means transparent/empty.

Stable IDs use lower snake case, begin with a letter, and contain neither repeated
nor trailing underscores. Once a level is published, node and connection IDs
should not be renamed: future editor data, score systems, and saves may refer to
them.

All referenced paths are relative to the JSON document containing the path. The
atlas path is therefore relative to `tileset.json`; after loading it is normalized
to a resource-root-relative path. Absolute paths, backslashes, empty components,
`.` and `..` components are rejected. JSON documents are limited to 16 MiB.

The runtime loader is authoritative and also performs checks JSON Schema cannot
fully express:

- every stamp row has one width, contains at least one nonzero tile, and its
  anchor selects an occupied cell;
- tile IDs/names/atlas cells, type IDs, level IDs, node IDs, edge IDs, directed
  endpoint pairs, and referenced document paths obey their respective uniqueness
  rules;
- atlas cells and node occupied cells are in bounds, and node masks do not overlap
  each other or solid tiles;
- `base_width >= tip_width`, and every authored edge has nonzero source/target
  capacity;
- candidate graphs are acyclic, root-reachable, and have clear solid-tile LOS.
