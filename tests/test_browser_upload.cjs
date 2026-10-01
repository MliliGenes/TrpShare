// Exercise the actual browser uploader with real HTTP and lost responses.
const fs = require('fs');
const os = require('os');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');
const {spawn} = require('child_process');
const assert = require('assert');

(async () => {
    const root = path.resolve(__dirname, '..');
    const share = fs.mkdtempSync(path.join(os.tmpdir(), 'trpshare-browser-'));
    fs.mkdirSync(path.join(share, 'nested'));
    const port = 20000 + process.pid % 20000;
    const server = spawn(process.argv[2] || path.join(root, 'trpshare'),
        ['--port', String(port), '--share', share], {cwd: root, stdio: 'ignore'});
    const base = 'http://127.0.0.1:' + port;
    const elements = new Map();
    const $ = name => {
        if (!elements.has(name)) elements.set(name, {
            textContent: '', value: 0, className: '', disabled: false,
            classList: {add() {}, remove() {}}
        });
        return elements.get(name);
    };
    const stored = new Map();
    let loseStart = true, loseChunk = true;
    const sandbox = {
        $, current: 'nested', fmt: String, load: async () => {},
        setTimeout, clearTimeout, AbortController, URLSearchParams, Uint8Array,
        crypto: crypto.webcrypto,
        localStorage: {getItem: key => stored.get(key) || null,
            setItem: (key, value) => stored.set(key, value), removeItem: key => stored.delete(key)},
        fetch: async (url, options = {}) => {
            const response = await fetch(base + url, options);
            if ((options.method === 'POST' && url.startsWith('/api/uploads?') && loseStart) ||
                (options.method === 'PUT' && loseChunk)) {
                if (options.method === 'POST') loseStart = false; else loseChunk = false;
                await response.arrayBuffer();
                throw new TypeError('simulated response lost after server commit');
            }
            return response;
        }
    };
    try {
        let ready = false;
        for (let i = 0; i < 100; ++i) {
            try { await fetch(base + '/api/files'); ready = true; break; }
            catch (_) { await new Promise(resolve => setTimeout(resolve, 25)); }
        }
        assert(ready, 'server did not start');
        vm.createContext(sandbox);
        vm.runInContext(fs.readFileSync(path.join(root, 'web/upload.js'), 'utf8'), sandbox);
        const bytes = crypto.randomBytes(16 * 1024 * 1024 + 23);
        const file = new Blob([bytes]);
        file.name = 'phone.bin'; file.lastModified = 12345;
        await assert.rejects(sandbox.uploadFile(file, 'nested'), /simulated response lost/);
        assert.strictEqual(stored.size, 1, 'session ID must survive a lost start response');
        await sandbox.uploadFile(file, 'nested');
        const saved = fs.readFileSync(path.join(share, 'nested/phone.bin'));
        assert.strictEqual(crypto.createHash('sha256').update(saved).digest('hex'),
            crypto.createHash('sha256').update(bytes).digest('hex'));
        assert.strictEqual(stored.size, 0);
        assert.strictEqual($('#upload-progress').value, 100);
        console.log('PASS: browser slices, persisted resume, lost start/chunk responses, exact file bytes');
    } finally {
        server.kill('SIGTERM');
        await new Promise(resolve => { if (server.exitCode !== null) resolve(); else server.once('exit', resolve); });
        fs.rmSync(share, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
