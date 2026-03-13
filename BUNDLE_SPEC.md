# WebZero Bundle Format Specification
## Version 1.0

### Overview

A `.web` file is a single binary file containing the complete compiled site.
The server `mmap()`s this file at startup. From that point on, zero file I/O
occurs during request handling — the OS page cache does all the work.

---

### File Layout

```
Offset  Size  Field
──────────────────────────────────────────────────────────
0       4     magic            0x57454230  ("WEB0", little-endian)
4       4     version          Must be 1
8       4     route_table_offset  Byte offset to ROUTE TABLE section
12      4     assets_offset    Byte offset to ASSETS section
16      4     handlers_offset  Byte offset to HANDLERS section
20      4     config_offset    Byte offset to CONFIG section
24      4     total_size       Total file size in bytes (for validation)
28      …     ROUTE TABLE
…       …     ASSETS TABLE
…       …     ASSETS DATA
…       …     HANDLERS TABLE  (may be empty)
…       …     HANDLERS DATA   (may be empty)
…       96    CONFIG
```

All integer fields are **little-endian**.

---

### ROUTE TABLE Section

A packed array of trie nodes. Each node is **64 bytes**:

```
Offset  Size  Field
──────────────────────────────────────────
0       32    segment    null-terminated path segment, e.g. "about"
               Node 0 is always the root node (segment = "")
32      2     child_count  Number of valid entries in children[]
34      16    children[8]  u16 indices into this same trie array
50      4     asset_idx    i32, -1 if not a leaf
54      4     handler_idx  i32, -1 if static
58      6     _pad         zero bytes for alignment
```

**Wildcard nodes**: A node with segment `"*"` matches any URL segment at
that position. Exact matches take priority over wildcards.

**Lookup algorithm**:
1. Start at node 0 (root).
2. For each path segment of the URL (split on `/`):
   a. Search current node's `children[]` for a node whose `segment` matches.
   b. If not found, look for a child with segment `"*"`.
   c. If still not found → 404.
3. Current node's `asset_idx` / `handler_idx` determines the response.

---

### ASSETS Section

Immediately follows the ROUTE TABLE. Layout:

**Asset Table**: a flat array of asset entries. Each entry is **56 bytes**:

```
Offset  Size  Field
──────────────────────────────────────────
0       4     offset           Byte offset within the asset DATA block
4       4     compressed_len   Compressed (brotli) byte count
8       4     original_len     Original uncompressed byte count
12      32    mime             Content-Type string, null-padded
44      1     encoding         0 = raw, 1 = brotli
45      3     _pad
```

**Asset Data block**: the concatenated compressed asset bytes.
The `offset` field in each entry is relative to the start of this data block,
which begins immediately after the asset table:

```
asset_data_start = assets_offset + asset_count * 56
asset_i_ptr      = base + asset_data_start + assets[i].offset
```

The server sends these bytes verbatim over the socket with
`Content-Encoding: br`.

---

### HANDLERS Section

A flat array of handler entries. Each entry is **8 bytes**:

```
Offset  Size  Field
──────────────────────────────────────────
0       4     offset   Byte offset to bytecode within handler DATA block
4       4     len      Byte length of bytecode
```

Handler bytecode follows the same layout as the asset data block.

#### Bytecode Instruction Set

| Opcode | Hex  | Operands              | Description                      |
|--------|------|-----------------------|----------------------------------|
| HALT   | 0x00 | —                     | Stop execution, send response    |
| PUSH_STR | 0x01 | u16 len, bytes      | Push string literal onto stack   |
| PUSH_INT | 0x02 | i32 value           | Push integer onto stack          |
| LOAD_REQ | 0x03 | u8 field            | Push request field (0=method, 1=path, 2=query, 3=body) |
| STORE_RES| 0x04 | u8 field            | Pop, store into response (0=status, 1=body, 2=content_type, 3=redirect) |
| ADD      | 0x05 | —                   | Pop two values, push sum or concat |
| EQ       | 0x06 | —                   | Pop two, push 1 if equal else 0  |
| JMP      | 0x07 | i16 rel_offset      | Unconditional jump               |
| JMP_IF   | 0x08 | i16 rel_offset      | Jump if top-of-stack is truthy   |
| SEND     | 0x09 | —                   | Finalize and send response       |
| REDIRECT | 0x0A | —                   | Pop URL, send 302                |
| GETPARAM | 0x0B | u8 namelen, bytes   | Push query param value by name   |
| RESPOND  | 0x0C | u16 status          | Pop body, send status + body     |

Stack depth: 32 values maximum. Values are either int32 or string (max 255 bytes).

---

### CONFIG Section

Fixed 96-byte structure at `config_offset`:

```
Offset  Size  Field
──────────────────────────────────────────────────────────
0       64    hostname           null-padded UTF-8 string
64      2     port               TCP port (default: 8080)
66      2     max_connections    Cap on simultaneous connections
68      4     keepalive_timeout_ms
72      4     asset_count        Number of entries in asset table
76      4     handler_count      Number of entries in handler table
80      4     route_node_count   Number of trie nodes
84      12    _reserved          zero bytes
```

---

### Validation

On load, the server checks:
1. `magic == 0x57454230`
2. `version == 1`
3. `total_size == file size on disk`
4. All section offsets are within `[0, total_size)`

If any check fails, the server prints an error and exits. The bundle is
never modified in place — it is always read-only (`PROT_READ` / `FILE_MAP_READ`).

---

### Tool Support

```bash
# Build a bundle from a source directory
node tools/wz.js build ./my-site

# Inspect a bundle
node tools/wz.js inspect ./my-site.web

# Serve a bundle for development (JS implementation, no C binary needed)
node tools/wz.js serve ./my-site.web --port 3000
```
