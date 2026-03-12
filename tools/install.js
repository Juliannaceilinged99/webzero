#!/usr/bin/env node
/**
 * tools/install.js — WebZero postinstall script
 * Runs automatically on: npm install -g webzero
 *
 * - Detects platform
 * - Checks ~/.webzero/version against package version
 * - Downloads binary from GitHub Releases if needed
 * - Shows ASCII progress bar (no dependencies)
 * - chmod +x on Linux/Mac
 */
'use strict';

const fs = require('fs');
const path = require('path');
const os = require('os');
const https = require('https');
const http = require('http');

/* ── Config ──────────────────────────────────────────────────────── */

var PKG_VERSION = '1.0.0';
try {
    var pkg = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'package.json'), 'utf8'));
    PKG_VERSION = pkg.version || PKG_VERSION;
} catch (_) { }

var GITHUB_REPO = 'davitotty/webzero';

var PLATFORM_MAP = {
    'linux-x64': 'webzero-linux-x64',
    'linux-arm': 'webzero-linux-arm',
    'win32-x64': 'webzero-windows-x64.exe',
    'win32-ia32': 'webzero-windows-x86.exe',
};

/* ── Helpers ─────────────────────────────────────────────────────── */

var PLATFORM = PLATFORM_MAP[process.platform + '-' + process.arch];
var WZ_DIR = path.join(os.homedir(), '.webzero');
var BIN_NAME = process.platform === 'win32' ? 'webzero.exe' : 'webzero';
var BIN_PATH = path.join(WZ_DIR, BIN_NAME);
var VER_PATH = path.join(WZ_DIR, 'version');

function readInstalledVersion() {
    try { return fs.existsSync(VER_PATH) ? fs.readFileSync(VER_PATH, 'utf8').trim() : null; }
    catch (_) { return null; }
}

function writeVersion(v) {
    try { fs.writeFileSync(VER_PATH, v); } catch (_) { }
}

function ensureDir(dir) {
    try { fs.mkdirSync(dir, { recursive: true }); } catch (_) { }
}

function renderProgress(received, total) {
    var BAR_WIDTH = 20;
    var pct = total > 0 ? Math.floor((received / total) * 100) : 0;
    var fill = total > 0 ? Math.floor((received / total) * BAR_WIDTH) : 0;
    var bar = '[' + '='.repeat(fill) + ' '.repeat(BAR_WIDTH - fill) + ']';
    var kb = (received / 1024).toFixed(0) + 'KB';
    process.stdout.write('\r' + bar + ' ' + pct + '% ' + kb + '   ');
}

/* ── Download ────────────────────────────────────────────────────── */

function downloadBinary(url, dest, callback) {
    var proto = url.startsWith('https') ? https : http;

    var req = proto.get(url, function (res) {
        // Follow redirects (GitHub releases use 302)
        if (res.statusCode === 301 || res.statusCode === 302) {
            return downloadBinary(res.headers.location, dest, callback);
        }

        if (res.statusCode !== 200) {
            return callback(new Error('HTTP ' + res.statusCode));
        }

        var total = parseInt(res.headers['content-length'] || '0', 10);
        var received = 0;
        var chunks = [];

        res.on('data', function (chunk) {
            chunks.push(chunk);
            received += chunk.length;
            renderProgress(received, total);
        });

        res.on('end', function () {
            process.stdout.write('\n');
            try {
                fs.writeFileSync(dest, Buffer.concat(chunks));
                callback(null);
            } catch (e) {
                callback(e);
            }
        });

        res.on('error', callback);
    });

    req.on('error', function (err) {
        callback(err);
    });

    // 60 second timeout
    req.setTimeout(60000, function () {
        req.abort();
        callback(new Error('Download timed out'));
    });
}

/* ── Main ────────────────────────────────────────────────────────── */

function install() {
    // Skip in CI environments where a binary isn't needed
    if (process.env.CI === 'true' || process.env.WEBZERO_SKIP_INSTALL === 'true') {
        console.log('webzero: skipping binary download (CI detected).');
        console.log('         Run "wz update" to install the binary manually.');
        return;
    }

    if (!PLATFORM) {
        console.warn('webzero: unsupported platform ' + process.platform + '-' + process.arch + '.');
        console.warn('         Binaries are available for: linux-x64, linux-arm, win32-x64, win32-ia32');
        console.warn('         The "wz build" and "wz inspect" commands still work without a binary.');
        return;
    }

    // Check if we already have the right version
    var installed = readInstalledVersion();
    if (installed === PKG_VERSION && fs.existsSync(BIN_PATH)) {
        console.log('webzero: binary v' + PKG_VERSION + ' already installed at ' + BIN_PATH);
        return;
    }

    if (installed && installed !== PKG_VERSION) {
        console.log('webzero: updating binary ' + installed + ' → ' + PKG_VERSION);
    }

    ensureDir(WZ_DIR);

    var url = 'https://github.com/' + GITHUB_REPO + '/releases/latest/download/' + PLATFORM;

    console.log('webzero: downloading binary for ' + process.platform + '-' + process.arch + '...');
    console.log('         ' + url);

    downloadBinary(url, BIN_PATH, function (err) {
        if (err) {
            console.error('\nwebzero: download failed: ' + err.message);
            console.error('         URL attempted: ' + url);
            console.error('');
            console.error('         To install manually:');
            console.error('           1. Download: ' + url);
            console.error('           2. Save to:  ' + BIN_PATH);
            if (process.platform !== 'win32') {
                console.error('           3. chmod +x ' + BIN_PATH);
            }
            console.error('');
            console.error('         "wz build" and "wz inspect" work without the binary.');
            // Don't exit with error — npm install should still succeed
            return;
        }

        // Make executable on Linux/Mac
        if (process.platform !== 'win32') {
            try {
                fs.chmodSync(BIN_PATH, 0o755);
            } catch (e) {
                console.warn('webzero: could not chmod binary: ' + e.message);
            }
        }

        writeVersion(PKG_VERSION);

        console.log('webzero: done. Binary saved to ' + BIN_PATH);
        console.log('         Run: wz --help');
    });
}

install();
