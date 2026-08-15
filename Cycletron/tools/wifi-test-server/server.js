// Minimal telemetry sink for the Cycletron WiFi/RF test (TEST_WIFI in
// src/main.cpp). No dependencies - uses only Node's built-in http module.
//
// Run:   node server.js
// Then point WIFI_TEST_URL in main.cpp at http://<this-pc-lan-ip>:3000/temp
// Open http://<this-pc-lan-ip>:3000/ in a browser to watch readings live.

const http = require('http');

const PORT = 3000;
const MAX_READINGS = 50;

const readings = [];

function escapeHtml(str) {
    return String(str).replace(/[&<>"']/g, (c) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[c]));
}

function renderPage() {
    const rows = readings.length === 0
        ? '<tr><td colspan="4">No readings yet. Waiting for the board to POST...</td></tr>'
        : readings.map((r) => `
            <tr>
                <td>${escapeHtml(r.timestamp)}</td>
                <td>${escapeHtml(r.ip)}</td>
                <td>${escapeHtml(r.device)}</td>
                <td>${escapeHtml(r.chip_temp_c)}</td>
            </tr>`).join('');

    return `<!doctype html>
<html>
<head>
    <meta charset="utf-8">
    <meta http-equiv="refresh" content="3">
    <title>Cycletron WiFi Test</title>
    <style>
        body { font-family: sans-serif; margin: 2rem; }
        table { border-collapse: collapse; width: 100%; max-width: 640px; }
        th, td { border: 1px solid #ccc; padding: 0.4rem 0.8rem; text-align: left; }
        th { background: #eee; }
    </style>
</head>
<body>
    <h1>Cycletron WiFi Test Server</h1>
    <p>Auto-refreshes every 3s. Most recent reading first.</p>
    <table>
        <tr><th>Time (UTC)</th><th>From IP</th><th>Device</th><th>Chip Temp (C)</th></tr>
        ${rows}
    </table>
</body>
</html>`;
}

const server = http.createServer((req, res) => {
    // Parse defensively - some HTTP clients send the request target in
    // absolute-form (http://host:port/path) instead of just the path,
    // which would otherwise fail a plain `req.url === '/temp'` check.
    const pathname = new URL(req.url, `http://${req.headers.host || 'localhost'}`).pathname;
    console.log(`[${new Date().toISOString()}] ${req.method} ${req.url} (path: ${pathname}) from ${req.socket.remoteAddress}`);

    if (req.method === 'POST' && pathname === '/temp') {
        let body = '';

        req.on('data', (chunk) => {
            body += chunk;
        });

        req.on('end', () => {
            const timestamp = new Date().toISOString();

            try {
                const data = JSON.parse(body);
                console.log(`[${timestamp}] ${req.socket.remoteAddress} -> ${JSON.stringify(data)}`);

                readings.unshift({
                    timestamp,
                    ip: req.socket.remoteAddress,
                    device: data.device ?? '(unknown)',
                    chip_temp_c: data.chip_temp_c ?? '(missing)',
                });
                readings.length = Math.min(readings.length, MAX_READINGS);
            } catch (err) {
                console.log(`[${timestamp}] ${req.socket.remoteAddress} -> (unparsable body) ${body}`);
            }

            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ status: 'ok' }));
        });

        return;
    }

    if (req.method === 'GET' && pathname === '/') {
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(renderPage());
        return;
    }

    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('Not found\n');
});

server.listen(PORT, '0.0.0.0', () => {
    console.log(`Cycletron WiFi test server listening on http://0.0.0.0:${PORT}`);
    console.log(`ESP should POST to http://<this-pc-lan-ip>:${PORT}/temp`);
    console.log(`Open http://<this-pc-lan-ip>:${PORT}/ in a browser to watch readings live.`);
});
