#!/usr/bin/env node
/**
 * wz.js — WebZero CLI
 * Zero npm dependencies. Node.js 14+ CommonJS only.
 *
 * Commands:
 *   wz build <dir>                   → <dir>.web bundle
 *   wz serve <file.web> [--port <n>] → spawns C binary
 *   wz inspect <file.web>            → dump bundle contents
 *   wz optimize <dir> [--widths w,…] → generate responsive JPEG variants via wzimg
 *   wz update                        → download/refresh binary
 *   wz version                       → print versions
 *
 * Image optimization (wz optimize + wz build):
 *   - wz optimize pre-generates resized JPEG variants using the wzimg C tool.
 *   - wz build automatically detects <name>.webp companions for any raster image
 *     and records the WebP asset index in the bundle so the C server can serve
 *     WebP automatically when a browser sends Accept: image/webp.
 *   - WebP conversion is done offline by the user (cwebp, squoosh, etc.).
 *     Zero does NOT encode WebP at runtime.
 */
'use strict';

const fs      = require('fs');
const path    = require('path');
const os      = require('os');
const zlib    = require('zlib');
const http    = require('http');
const https   = require('https');
const { spawn, spawnSync } = require('child_process');

/* ── Constants ─────────────────────────────────────────────────────── */

const WZ_VERSION        = '1.0.0';
const WEB_MAGIC         = 0x57454230;
const WEB_VERSION       = 1;
const DISK_TRIE_NODE_SIZE = 64;

/*
 * Asset entry on-disk layout — 56 bytes, matches AssetEntry in core/bundle.h:
 *   [0 ]  offset         uint32
 *   [4 ]  compressed_len uint32
 *   [8 ]  original_len   uint32
 *   [12]  mime[32]       char[32]
 *   [44]  encoding       uint8   (0=raw, 1=brotli)
 *   [45]  _pad1[3]       uint8[3]
 *   [48]  webp_idx       int32   (-1 = no WebP variant)
 *   [52]  _pad2[4]       uint8[4]
 */
const ASSET_ENTRY_SIZE  = 56;

/* Raster image extensions that can have a .webp companion */
const RASTER_EXTS = new Set(['.jpg', '.jpeg', '.png', '.gif', '.bmp']);

/* Default responsive breakpoint widths for wz optimize */
const DEFAULT_WIDTHS = [320, 640, 1280];

const GITHUB_REPO = 'davitotty/webzero';

const PLATFORM_MAP = {
    'linux-x64':  'webzero-linux-x64',
    'linux-arm':  'webzero-linux-arm',
    'win32-x64':  'webzero-windows-x64.exe',
    'win32-ia32': 'webzero-windows-x86.exe',
};

const MIME_MAP = {
    '.html': 'text/html; charset=utf-8', '.htm': 'text/html; charset=utf-8',
    '.css': 'text/css', '.js': 'application/javascript', '.json': 'application/json',
    '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
    '.gif': 'image/gif', '.webp': 'image/webp', '.svg': 'image/svg+xml',
    '.ico': 'image/x-icon', '.woff': 'font/woff', '.woff2': 'font/woff2',
    '.ttf': 'font/ttf', '.txt': 'text/plain; charset=utf-8',
    '.xml': 'application/xml', '.pdf': 'application/pdf',
    '.mp4': 'video/mp4', '.webm': 'video/webm',
};

function mimeOf(f) { return MIME_MAP[path.extname(f).toLowerCase()] || 'application/octet-stream'; }

/* ── Binary resolution ─────────────────────────────────────────────── */

function getBinaryPath() {
    const binName = process.platform === 'win32' ? 'webzero.exe' : 'webzero';
    return path.join(os.homedir(), '.webzero', binName);
}

function getBinaryVersion() {
    try {
        const vf = path.join(os.homedir(), '.webzero', 'version');
        return fs.existsSync(vf) ? fs.readFileSync(vf, 'utf8').trim() : null;
    } catch (_) { return null; }
}

function getPlatformName() {
    return PLATFORM_MAP[`${process.platform}-${process.arch}`] || null;
}

/* ── Trie helpers ──────────────────────────────────────────────────── */

function urlPathFromFile(sourceDir, filePath) {
    let rel = path.relative(sourceDir, filePath).replace(/\\/g, '/');
    if (rel.endsWith('.html')) rel = rel.slice(0, -5);
    if (!rel.startsWith('/')) rel = '/' + rel;
    return rel;
}

class TrieNode {
    constructor(seg) { this.segment = seg || ''; this.children = {}; this.assetIdx = -1; this.handlerIdx = -1; }
}

function trieInsert(root, urlPath, assetIdx, handlerIdx) {
    const segs = urlPath.split('/').filter(Boolean);
    let node = root;
    for (const seg of segs) {
        if (!node.children[seg]) node.children[seg] = new TrieNode(seg);
        node = node.children[seg];
    }
    node.assetIdx = assetIdx; node.handlerIdx = handlerIdx;
}

function flattenTrie(root) {
    const nodes = []; const queue = [root]; const indexMap = new Map();
    while (queue.length > 0) {
        const node = queue.shift(); indexMap.set(node, nodes.length); nodes.push(node);
        for (const c of Object.values(node.children)) queue.push(c);
    }
    return nodes.map(n => ({
        segment: n.segment,
        children: Object.values(n.children).map(c => indexMap.get(c)),
        assetIdx: n.assetIdx, handlerIdx: n.handlerIdx,
    }));
}

function serializeTrie(nodes) {
    const buf = Buffer.alloc(nodes.length * DISK_TRIE_NODE_SIZE, 0);
    for (let i = 0; i < nodes.length; i++) {
        const off = i * DISK_TRIE_NODE_SIZE; const node = nodes[i];
        Buffer.from(node.segment, 'utf8').copy(buf, off, 0, 31);
        const cc = Math.min(node.children.length, 8);
        buf.writeUInt16LE(cc, off + 32);
        for (let j = 0; j < cc; j++) buf.writeUInt16LE(node.children[j], off + 34 + j * 2);
        buf.writeInt32LE(node.assetIdx, off + 50);
        buf.writeInt32LE(node.handlerIdx, off + 54);
    }
    return buf;
}

/* ── Compression ───────────────────────────────────────────────────── */

function brotliCompress(data) {
    return new Promise((res, rej) => zlib.brotliCompress(data, {
        params: { [zlib.constants.BROTLI_PARAM_QUALITY]: 11 }
    }, (e, r) => e ? rej(e) : res(r)));
}

/* ── Build ─────────────────────────────────────────────────────────── */

async function cmdBuild(sourceDir) {
    if (!fs.existsSync(sourceDir)) {
        console.error('wz: source directory not found: ' + sourceDir); process.exit(1);
    }
    console.log('wz: building ' + sourceDir + '...');

    function walk(dir, out) {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, e.name);
            if (e.isDirectory()) walk(full, out); else if (e.isFile()) out.push(full);
        }
    }

    const allFiles = [];
    walk(sourceDir, allFiles);

    /* Filter out .wz scratch files */
    const files = allFiles.filter(f => path.extname(f).toLowerCase() !== '.wz');

    /*
     * Pre-assign indices so WebP companion lookups work during the single
     * compression pass below (we need to know what index a .webp will get
     * before we compress the original raster image).
     */
    const pathToIdx = new Map();
    files.forEach((f, i) => pathToIdx.set(path.resolve(f), i));

    const assetChunks  = [];
    const assetEntries = [];
    const handlerEntries = [];
    const trieRoot     = new TrieNode('');
    let   currentOffset = 0;

    for (const file of files) {
        const raw        = fs.readFileSync(file);
        const compressed = await brotliCompress(raw);
        const urlPath    = urlPathFromFile(sourceDir, file);
        const assetIdx   = assetEntries.length;
        const ext        = path.extname(file).toLowerCase();

        /*
         * WebP companion detection:
         * If this is a raster image (jpg/jpeg/png/gif/bmp) and a .webp file
         * with the same base name exists in the source tree, record its future
         * asset index so the C server can perform Accept-based negotiation.
         */
        let webpIdx = -1;
        if (RASTER_EXTS.has(ext)) {
            const webpPath = path.resolve(file.slice(0, file.length - ext.length) + '.webp');
            if (pathToIdx.has(webpPath)) {
                webpIdx = pathToIdx.get(webpPath);
            }
        }

        assetEntries.push({
            offset:         currentOffset,
            compressed_len: compressed.length,
            original_len:   raw.length,
            mime:           mimeOf(file),
            encoding:       1,
            webpIdx:        webpIdx,
        });
        assetChunks.push(compressed);
        currentOffset += compressed.length;
        trieInsert(trieRoot, urlPath, assetIdx, -1);

        const webpNote = webpIdx >= 0 ? ' [webp→' + webpIdx + ']' : '';
        console.log('  + ' + urlPath.padEnd(40) + raw.length + 'B → ' + compressed.length + 'B (br)' + webpNote);
    }

    const trieNodes  = flattenTrie(trieRoot);
    const trieBytes  = serializeTrie(trieNodes);

    const configBuf = Buffer.alloc(96, 0);
    Buffer.from('localhost').copy(configBuf, 0, 0, 63);
    configBuf.writeUInt16LE(8080, 64);  configBuf.writeUInt16LE(256, 66);
    configBuf.writeUInt32LE(30000, 68); configBuf.writeUInt32LE(assetEntries.length, 72);
    configBuf.writeUInt32LE(handlerEntries.length, 76); configBuf.writeUInt32LE(trieNodes.length, 80);

    /*
     * Serialize asset table.  Layout per entry (56 bytes):
     *   [0 ] offset         uint32
     *   [4 ] compressed_len uint32
     *   [8 ] original_len   uint32
     *   [12] mime[32]       char[32]
     *   [44] encoding       uint8
     *   [45] _pad1[3]       (zeros)
     *   [48] webp_idx       int32
     *   [52] _pad2[4]       (zeros)
     */
    const assetTableBuf = Buffer.alloc(assetEntries.length * ASSET_ENTRY_SIZE, 0);
    for (let i = 0; i < assetEntries.length; i++) {
        const e   = assetEntries[i];
        const off = i * ASSET_ENTRY_SIZE;
        assetTableBuf.writeUInt32LE(e.offset,         off);
        assetTableBuf.writeUInt32LE(e.compressed_len, off + 4);
        assetTableBuf.writeUInt32LE(e.original_len,   off + 8);
        Buffer.from(e.mime).copy(assetTableBuf, off + 12, 0, 31);
        assetTableBuf.writeUInt8(e.encoding, off + 44);
        /* _pad1[3] at [45..47] already zero */
        assetTableBuf.writeInt32LE(e.webpIdx, off + 48);
        /* _pad2[4] at [52..55] already zero */
    }

    const assetDataBuf = Buffer.concat(assetChunks);

    const HEADER_SIZE  = 28;
    const trieOff      = HEADER_SIZE;
    const assetsOff    = trieOff + trieBytes.length;
    const handlersOff  = assetsOff + assetTableBuf.length + assetDataBuf.length;
    const configOff    = handlersOff;
    const totalSize    = configOff + configBuf.length;

    const header = Buffer.alloc(HEADER_SIZE, 0);
    header.writeUInt32LE(WEB_MAGIC,   0);  header.writeUInt32LE(WEB_VERSION, 4);
    header.writeUInt32LE(trieOff,     8);  header.writeUInt32LE(assetsOff,  12);
    header.writeUInt32LE(handlersOff, 16); header.writeUInt32LE(configOff,  20);
    header.writeUInt32LE(totalSize,   24);

    const bundle  = Buffer.concat([header, trieBytes, assetTableBuf, assetDataBuf, configBuf]);
    const outPath = sourceDir.replace(/[/\\]$/, '') + '.web';
    fs.writeFileSync(outPath, bundle);

    console.log('\nwz: bundle written to ' + outPath);
    console.log('    total size : ' + bundle.length + ' bytes');
    console.log('    assets     : ' + assetEntries.length);
    console.log('    trie nodes : ' + trieNodes.length);
    console.log('    handlers   : ' + handlerEntries.length);
}

/* ── Inspect ───────────────────────────────────────────────────────── */

function cmdInspect(bundlePath) {
    if (!fs.existsSync(bundlePath)) {
        console.error('wz: bundle not found: ' + bundlePath); process.exit(1);
    }
    const buf        = fs.readFileSync(bundlePath);
    const magic      = buf.readUInt32LE(0);  const version   = buf.readUInt32LE(4);
    const trieOff    = buf.readUInt32LE(8);  const assetsOff = buf.readUInt32LE(12);
    const handlersOff = buf.readUInt32LE(16); const configOff = buf.readUInt32LE(20);
    const totalSize  = buf.readUInt32LE(24);

    console.log('=== WebZero Bundle: ' + bundlePath + ' ===');
    console.log('magic          : 0x' + magic.toString(16).toUpperCase());
    console.log('version        : ' + version);
    console.log('total size     : ' + totalSize + ' bytes');
    console.log('route_table_at : ' + trieOff);
    console.log('assets_at      : ' + assetsOff);
    console.log('handlers_at    : ' + handlersOff);
    console.log('config_at      : ' + configOff);

    const hostname     = buf.slice(configOff, configOff + 64).toString('utf8').replace(/\0.*$/, '');
    const port         = buf.readUInt16LE(configOff + 64);
    const maxConn      = buf.readUInt16LE(configOff + 66);
    const keepaliveMs  = buf.readUInt32LE(configOff + 68);
    const assetCount   = buf.readUInt32LE(configOff + 72);
    const handlerCount = buf.readUInt32LE(configOff + 76);
    const trieNodeCount = buf.readUInt32LE(configOff + 80);

    console.log('\nConfig:');
    console.log('  hostname     : ' + hostname);
    console.log('  port         : ' + port);
    console.log('  max_conn     : ' + maxConn);
    console.log('  keepalive_ms : ' + keepaliveMs);
    console.log('  assets       : ' + assetCount);
    console.log('  handlers     : ' + handlerCount);
    console.log('  trie nodes   : ' + trieNodeCount);

    console.log('\nAssets:');
    for (let i = 0; i < assetCount; i++) {
        const base    = assetsOff + i * ASSET_ENTRY_SIZE;
        const clen    = buf.readUInt32LE(base + 4);
        const olen    = buf.readUInt32LE(base + 8);
        const mime    = buf.slice(base + 12, base + 44).toString('utf8').replace(/\0.*$/, '');
        const enc     = buf.readUInt8(base + 44);
        const webpIdx = buf.readInt32LE(base + 48);
        const ratio   = olen > 0 ? ((1 - clen / olen) * 100).toFixed(1) : '0.0';
        const webpNote = webpIdx >= 0 ? ' webp→[' + webpIdx + ']' : '';
        console.log('  [' + i + '] ' + mime.padEnd(38) + ' ' + olen + 'B → ' + clen + 'B (−' + ratio + '%) enc=' + enc + webpNote);
    }

    console.log('\nTrie Nodes:');
    for (let i = 0; i < trieNodeCount; i++) {
        const base = trieOff + i * DISK_TRIE_NODE_SIZE;
        const seg  = buf.slice(base, base + 32).toString('utf8').replace(/\0.*$/, '') || '(root)';
        const cc   = buf.readUInt16LE(base + 32);
        const ai   = buf.readInt32LE(base + 50);
        const hi   = buf.readInt32LE(base + 54);
        const ch   = []; for (let j = 0; j < cc; j++) ch.push(buf.readUInt16LE(base + 34 + j * 2));
        console.log('  [' + i + '] "' + seg + '" children=[' + ch.join(',') + '] asset=' + ai + ' handler=' + hi);
    }
}

/* ── Serve ─────────────────────────────────────────────────────────── */

function printStartupBanner(bundleFile, port, routeCount) {
    const bname = path.basename(bundleFile);
    const ver   = 'v' + WZ_VERSION;
    process.stdout.write('\n');
    process.stdout.write('┌─────────────────────────────────┐\n');
    process.stdout.write('│  WebZero ' + ver.padEnd(23) + '│\n');
    process.stdout.write('│  bundle : ' + bname.padEnd(22) + '│\n');
    process.stdout.write('│  port   : ' + String(port).padEnd(22) + '│\n');
    process.stdout.write('│  routes : ' + String(routeCount).padEnd(22) + '│\n');
    process.stdout.write('│  memory : 4.0 MB reserved        │\n');
    process.stdout.write('│  ready  ✓                        │\n');
    process.stdout.write('└─────────────────────────────────┘\n');
    process.stdout.write('\n');
}

function cmdServe(bundleFile, port) {
    try {
        if (!fs.existsSync(bundleFile)) {
            console.error('wz: bundle not found: ' + bundleFile); process.exit(1);
        }

        const buf          = fs.readFileSync(bundleFile);
        const configOff    = buf.readUInt32LE(20);
        const assetCount   = buf.readUInt32LE(configOff + 72);
        const trieNodeCount = buf.readUInt32LE(configOff + 80);
        const assetsOff    = buf.readUInt32LE(12);
        const trieOff      = buf.readUInt32LE(8);

        /* Load assets (include webpIdx for Accept negotiation) */
        const assets    = [];
        const dataStart = assetsOff + assetCount * ASSET_ENTRY_SIZE;
        for (let i = 0; i < assetCount; i++) {
            const base    = assetsOff + i * ASSET_ENTRY_SIZE;
            const offset  = buf.readUInt32LE(base);
            const clen    = buf.readUInt32LE(base + 4);
            const olen    = buf.readUInt32LE(base + 8);
            const mime    = buf.slice(base + 12, base + 44).toString('utf8').replace(/\0.*$/, '');
            const enc     = buf.readUInt8(base + 44);
            const webpIdx = buf.readInt32LE(base + 48);
            const data    = buf.slice(dataStart + offset, dataStart + offset + clen);
            assets.push({ mime, enc, data, olen, webpIdx });
        }

        /* Load trie */
        const trieNodes = [];
        for (let i = 0; i < trieNodeCount; i++) {
            const base     = trieOff + i * DISK_TRIE_NODE_SIZE;
            const seg      = buf.slice(base, base + 32).toString('utf8').replace(/\0.*$/, '');
            const cc       = buf.readUInt16LE(base + 32);
            const children = [];
            for (let j = 0; j < cc; j++) children.push(buf.readUInt16LE(base + 34 + j * 2));
            const ai       = buf.readInt32LE(base + 50);
            trieNodes.push({ seg, children, ai });
        }

        function lookup(urlPath) {
            const segs = urlPath.split('/').filter(Boolean);
            let node   = trieNodes[0];
            for (const seg of segs) {
                const child = node.children.map(i => trieNodes[i]).find(n => n.seg === seg);
                if (!child) return -1;
                node = child;
            }
            return node.ai;
        }

        printStartupBanner(bundleFile, port, trieNodeCount);

        const server = http.createServer(function(req, res) {
            let urlPath = req.url.split('?')[0];
            if (urlPath === '/') urlPath = '/index';

            let ai = lookup(urlPath);
            if (ai === -1 && !urlPath.includes('.')) ai = lookup(urlPath + '/index');
            if (ai === -1) { res.writeHead(404); res.end('Not Found'); return; }

            let asset = assets[ai];

            /*
             * WebP content negotiation: if the client accepts image/webp and this
             * asset has a WebP companion, serve the WebP variant transparently.
             */
            const acceptHeader  = req.headers['accept'] || '';
            const acceptsWebP   = acceptHeader.includes('image/webp');
            if (acceptsWebP && asset.webpIdx >= 0 && asset.webpIdx < assets.length) {
                asset = assets[asset.webpIdx];
            }

            const acceptsBrotli = (req.headers['accept-encoding'] || '').includes('br');

            if (asset.enc === 1 && acceptsBrotli) {
                res.writeHead(200, {
                    'Content-Type':     asset.mime,
                    'Content-Encoding': 'br',
                    'Content-Length':   asset.data.length,
                    'Cache-Control':    'max-age=3600',
                    'Vary':             'Accept, Accept-Encoding',
                });
                res.end(asset.data);
            } else if (asset.enc === 1) {
                zlib.brotliDecompress(asset.data, function(err, decoded) {
                    if (err) { res.writeHead(500); res.end('decompress error'); return; }
                    res.writeHead(200, {
                        'Content-Type':   asset.mime,
                        'Content-Length': decoded.length,
                        'Cache-Control':  'max-age=3600',
                        'Vary':           'Accept, Accept-Encoding',
                    });
                    res.end(decoded);
                });
            } else {
                res.writeHead(200, { 'Content-Type': asset.mime, 'Content-Length': asset.data.length });
                res.end(asset.data);
            }
        });

        server.listen(port, function() {});
        process.on('SIGINT',  function() { server.close(); process.exit(0); });
        process.on('SIGTERM', function() { server.close(); process.exit(0); });

    } catch (err) {
        console.error('wz serve error: ' + err.message); process.exit(1);
    }
}

/* ── Optimize ──────────────────────────────────────────────────────── */

/*
 * wz optimize <dir> [--widths 320,640,1280] [--quality 82]
 *
 * Walks <dir> for raster images and generates responsive JPEG variants
 * at each specified width using the wzimg C tool.  Generated files are
 * placed alongside the source image as:
 *   hero.jpg → hero@320w.jpg, hero@640w.jpg, hero@1280w.jpg
 *
 * Run this before `wz build` so the variants get bundled automatically.
 * To also get WebP variants, run cwebp/squoosh on each output afterwards.
 *
 * wzimg must be compiled and on $PATH or in the project root:
 *   cc -O2 -std=c99 -o wzimg tools/wzimg.c
 */
function cmdOptimize(sourceDir, widths, quality) {
    if (!fs.existsSync(sourceDir)) {
        console.error('wz: source directory not found: ' + sourceDir); process.exit(1);
    }

    /* Locate wzimg binary: project root → $PATH */
    function findWzimg() {
        const local = path.join(process.cwd(), process.platform === 'win32' ? 'wzimg.exe' : 'wzimg');
        if (fs.existsSync(local)) return local;
        /* Try PATH by running a no-op probe */
        const probe = spawnSync(process.platform === 'win32' ? 'where' : 'which',
                                ['wzimg'], { encoding: 'utf8' });
        if (probe.status === 0 && probe.stdout.trim()) return 'wzimg';
        return null;
    }

    const wzimg = findWzimg();
    if (!wzimg) {
        console.error('wz: wzimg not found. Build it first:');
        console.error('      cc -O2 -std=c99 -o wzimg tools/wzimg.c');
        process.exit(1);
    }

    function walk(dir, out) {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, e.name);
            if (e.isDirectory()) walk(full, out); else if (e.isFile()) out.push(full);
        }
    }

    const allFiles = [];
    walk(sourceDir, allFiles);
    const images = allFiles.filter(f => RASTER_EXTS.has(path.extname(f).toLowerCase()));

    if (images.length === 0) {
        console.log('wz optimize: no raster images found in ' + sourceDir);
        return;
    }

    console.log('wz optimize: processing ' + images.length + ' image(s) at widths [' + widths.join(', ') + '] q=' + quality);

    let generated = 0;
    let skipped   = 0;

    for (const img of images) {
        const ext  = path.extname(img);
        const base = img.slice(0, img.length - ext.length);

        for (const w of widths) {
            const outPath = base + '@' + w + 'w.jpg';

            /* Skip if already up-to-date (output newer than source) */
            if (fs.existsSync(outPath)) {
                const srcMtime = fs.statSync(img).mtimeMs;
                const outMtime = fs.statSync(outPath).mtimeMs;
                if (outMtime >= srcMtime) {
                    skipped++;
                    continue;
                }
            }

            const result = spawnSync(wzimg, [img, outPath, String(w), String(quality)], {
                encoding: 'utf8',
                stdio:    ['ignore', 'pipe', 'pipe'],
            });

            if (result.status !== 0) {
                console.error('wz: wzimg failed for ' + path.basename(img) + ' @' + w + 'w:');
                if (result.stderr) process.stderr.write(result.stderr);
                continue;
            }

            const rel = path.relative(sourceDir, outPath);
            console.log('  → ' + rel + (result.stderr ? '  ' + result.stderr.trim() : ''));
            generated++;
        }
    }

    console.log('\nwz optimize: ' + generated + ' generated, ' + skipped + ' up-to-date');
    console.log('Next: run `wz build ' + sourceDir + '` to bundle the variants.');
}

/* ── Update (download binary) ──────────────────────────────────────── */

function downloadFile(url, destPath, cb) {
    const proto = url.startsWith('https') ? https : http;
    proto.get(url, function (res) {
        if (res.statusCode === 301 || res.statusCode === 302) {
            return downloadFile(res.headers.location, destPath, cb);
        }
        if (res.statusCode !== 200) {
            return cb(new Error('HTTP ' + res.statusCode + ' from ' + url));
        }

        const total    = parseInt(res.headers['content-length'] || '0', 10);
        let received   = 0;
        const chunks   = [];
        const BAR_WIDTH = 20;

        res.on('data', function (chunk) {
            chunks.push(chunk); received += chunk.length;
            if (total > 0) {
                const pct  = Math.floor((received / total) * 100);
                const fill = Math.floor((received / total) * BAR_WIDTH);
                const bar  = '[' + '='.repeat(fill) + ' '.repeat(BAR_WIDTH - fill) + ']';
                const kb   = (received / 1024).toFixed(0) + 'KB';
                process.stdout.write('\r' + bar + ' ' + pct + '% ' + kb + '   ');
            }
        });
        res.on('end', function () {
            process.stdout.write('\n');
            try { fs.writeFileSync(destPath, Buffer.concat(chunks)); cb(null); }
            catch (e) { cb(e); }
        });
        res.on('error', cb);
    }).on('error', cb);
}

function cmdUpdate() {
    try {
        const platformName = getPlatformName();
        if (!platformName) {
            console.error('wz: unsupported platform: ' + process.platform + '-' + process.arch);
            console.error('    Supported: linux-x64, linux-arm, win32-x64, win32-ia32');
            process.exit(1);
        }

        const wzDir   = path.join(os.homedir(), '.webzero');
        const binExt  = process.platform === 'win32' ? 'webzero.exe' : 'webzero';
        const binPath = path.join(wzDir, binExt);
        const verPath = path.join(wzDir, 'version');

        if (!fs.existsSync(wzDir)) fs.mkdirSync(wzDir, { recursive: true });

        const url = 'https://github.com/' + GITHUB_REPO + '/releases/latest/download/' + platformName;
        console.log('Downloading webzero binary for ' + process.platform + '-' + process.arch + '...');
        console.log('URL: ' + url);

        downloadFile(url, binPath, function (err) {
            if (err) {
                console.error('\nwz: download failed: ' + err.message);
                console.error('    Attempted URL: ' + url);
                console.error('    You can manually download and place at: ' + binPath);
                process.exit(1);
            }

            if (process.platform !== 'win32') {
                try { fs.chmodSync(binPath, 0o755); } catch (_) { }
            }

            try { fs.writeFileSync(verPath, WZ_VERSION); } catch (_) { }

            console.log('Done. Binary saved to ' + binPath);
            console.log('Version: ' + WZ_VERSION);
        });
    } catch (err) {
        console.error('wz update error: ' + err.message); process.exit(1);
    }
}

/* ── Version ───────────────────────────────────────────────────────── */

function cmdVersion() {
    console.log('wz CLI    : v' + WZ_VERSION);
    const binVer  = getBinaryVersion();
    const binPath = getBinaryPath();
    if (binVer) {
        console.log('binary    : v' + binVer + ' (' + binPath + ')');
    } else if (fs.existsSync(binPath)) {
        console.log('binary    : installed (version unknown) — ' + binPath);
    } else {
        console.log('binary    : not installed — run: wz update');
    }
    console.log('node      : ' + process.version);
    console.log('platform  : ' + process.platform + '-' + process.arch);
}

/* ── CLI entry ─────────────────────────────────────────────────────── */

try {
    const args = process.argv.slice(2);
    const cmd  = args[0];

    function getPort(defaultPort) {
        const idx = args.indexOf('--port');
        if (idx !== -1 && args[idx + 1]) return parseInt(args[idx + 1], 10);
        for (let i = 2; i < args.length; i++) {
            const n = parseInt(args[i], 10);
            if (!isNaN(n) && n > 0 && n < 65536) return n;
        }
        return defaultPort;
    }

    function getFlag(name, defaultVal) {
        const idx = args.indexOf(name);
        if (idx !== -1 && args[idx + 1]) return args[idx + 1];
        return defaultVal;
    }

    if (!cmd || cmd === '--help' || cmd === '-h') {
        process.stdout.write([
            '',
            '  WebZero CLI v' + WZ_VERSION,
            '',
            '  wz build <dir>                   build a .web bundle from a directory',
            '  wz serve <file.web> [--port <n>] start the server (spawns C binary)',
            '  wz inspect <file.web>            dump bundle contents',
            '  wz optimize <dir> [options]      generate responsive JPEG variants',
            '    --widths 320,640,1280           comma-separated target widths (px)',
            '    --quality 82                    JPEG quality 1-100',
            '  wz update                        download/refresh C binary for this platform',
            '  wz version                       print CLI and binary versions',
            '',
            '  Image optimization workflow:',
            '    1. wz optimize ./my-site        generate @320w/@640w/@1280w variants',
            '    2. (optional) run cwebp on images to create .webp companions',
            '    3. wz build ./my-site           bundle everything; WebP auto-detected',
            '',
        ].join('\n') + '\n');
        process.exit(0);
    }

    switch (cmd) {
        case 'build':
            if (!args[1]) { console.error('wz: build requires a source directory'); process.exit(1); }
            cmdBuild(args[1]).catch(function (e) { console.error('wz build error: ' + e.message); process.exit(1); });
            break;

        case 'serve':
            if (!args[1]) { console.error('wz: serve requires a bundle path'); process.exit(1); }
            cmdServe(args[1], getPort(8080));
            break;

        case 'inspect':
            if (!args[1]) { console.error('wz: inspect requires a bundle path'); process.exit(1); }
            cmdInspect(args[1]);
            break;

        case 'optimize': {
            if (!args[1]) { console.error('wz: optimize requires a source directory'); process.exit(1); }
            const rawWidths = getFlag('--widths', DEFAULT_WIDTHS.join(','));
            const widths    = String(rawWidths).split(',').map(Number).filter(w => w > 0 && w < 16384);
            const quality   = parseInt(getFlag('--quality', '82'), 10);
            if (widths.length === 0) { console.error('wz: --widths must be comma-separated positive integers'); process.exit(1); }
            if (isNaN(quality) || quality < 1 || quality > 100) { console.error('wz: --quality must be 1-100'); process.exit(1); }
            cmdOptimize(args[1], widths, quality);
            break;
        }

        case 'update':
            cmdUpdate();
            break;

        case 'version':
        case '-v':
        case '--version':
            cmdVersion();
            break;

        default:
            console.error('wz: unknown command \'' + cmd + '\'. Run: wz --help');
            process.exit(1);
    }
} catch (err) {
    console.error('wz: unexpected error: ' + err.message);
    process.exit(1);
}
