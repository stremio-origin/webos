// Compares this build's log scrubbing and sealing with the web app's own code (the functions below are
// copied from its helper unchanged): node tools/logs-check.js build/logs-test   (run by tools/logs-check.sh)
var cp = require('child_process'), fs = require('fs'), os = require('os'), path = require('path'), crypto = require('crypto');
var zlib = require('zlib'); /* the developer's opener, its decryption alone (the tool itself and the key stay private) */
function open(code, page, privB64) { // page: base64 of SBL3 | one-time key (65) | iv (12) | tag (16) | sealed
  code = String(code).toUpperCase().replace(/[^0-9A-Z]/g, '');
  var b = Buffer.from(String(page).trim(), 'base64');
  if (b.length < 97 || b.slice(0, 4).toString() !== 'SBL3') throw new Error('not a sealed log page');
  var eph = b.slice(4, 69), ec = crypto.createECDH('prime256v1'); ec.setPrivateKey(Buffer.from(privB64, 'base64'));
  var shared = ec.computeSecret(eph);
  var key = crypto.createHash('sha256').update('stremioorigin-logs-v1|' + code + '|').update(eph).update(shared).digest();
  var d = crypto.createDecipheriv('aes-256-gcm', key, b.slice(69, 81)); d.setAAD(Buffer.from('SBL3|' + code, 'utf8')); d.setAuthTag(b.slice(81, 97));
  return zlib.gunzipSync(Buffer.concat([d.update(b.slice(97)), d.final()])).toString('utf8');
}
var BIN = process.argv[2] || 'build/logs-test', fails = 0;
function scrub(t) {
  return String(t || '').slice(0, 2000)
    .replace(/(https?:\/\/[^\s"'<>?#]+)[?#][^\s"'<>]*/g, '$1?…') // query strings carry keys / tokens
    .replace(/[A-Za-z0-9_\-]{24,}/g, '…') // any long key-like run (debrid keys, addon configs, tokens, hashes)
    .replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, '<email>')
    .replace(/[\u0000-\u0008\u000b-\u001f]/g, ' ');
}
function lsScrub(t) {
  return String(t)
    .replace(/(authKey|auth_key|apikey|api_key|token|access_token|refresh_token|password|passwd|secret|bearer)(["'\s:=]+)([^\s"'&,;<>]{4,})/gi, '$1$2[redacted]')
    .replace(/https?:\/\/[^\s"'<>]+/g, function (u) { // a link keeps its host and plain path: query, fragment, credentials and token-like parts go
      var m = /^(https?:\/\/)(?:[^@\/]*@)?([^\/?#]+)([^?#]*)/.exec(u); if (!m) return '[link]';
      var p = m[3].split('/').map(function (seg) { return (seg.length >= 20 || /[=|%]|^[A-Za-z0-9+_-]{16,}$/.test(seg)) ? '[…]' : seg; }).join('/');
      return m[1] + m[2] + p; })
    .replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, '[email]')
    .replace(/\b(?:\d{1,3}\.){3}\d{1,3}\b/g, '[ip]')
    .replace(/\b[0-9a-fA-F]{40}\b/g, function (h) { return h.slice(0, 8) + '…'; })
    .replace(/\b(?=[A-Za-z0-9+_-]*\d)(?=[A-Za-z0-9+_-]*[A-Za-z])[A-Za-z0-9+_-]{24,}={0,2}/g, '[redacted]'); // token-like runs (letters and digits)
}
function run(args) { return cp.execFileSync(BIN, args, { encoding: 'utf8' }).trim(); }
function ok(name, good, extra) { if (!good) fails++; console.log((good ? 'ok   ' : 'FAIL ') + name + (extra ? '  ' + extra : '')); }

// 1. the published vectors
try { run(['vectors']); ok('published vectors', true); } catch (e) { ok('published vectors', false); console.log(e.stdout || e.message); }
// 2. random AES-GCM and PBKDF2 cases against Node's OpenSSL
var bad = 0;
for (var i = 0; i < 40; i++) {
  var k = crypto.randomBytes(32), iv = crypto.randomBytes(12), a = crypto.randomBytes(i % 37), p = crypto.randomBytes((i * 13) % 300);
  var c = crypto.createCipheriv('aes-256-gcm', k, iv); c.setAAD(a); var ct = Buffer.concat([c.update(p), c.final()]).toString('hex'), tag = c.getAuthTag().toString('hex');
  if (run(['gcm', k.toString('hex'), iv.toString('hex'), a.toString('hex') || '', p.toString('hex') || '']) !== (ct + ' ' + tag).trim()) bad++;
}
ok('aes-256-gcm vs node, 40 random cases', !bad, bad ? bad + ' differ' : '');
bad = 0;
[['ABCDEFGHJKMN', 'stremioorigin-logs-v1|sbl0123', 1000], ['x', 'y', 1], ['0123456789ABCDEFGHJKMNPQRSTVWXYZ0123456789ABCDEFGHJKMNPQRSTVWXYZ0123', 'salt', 7]].forEach(function (t) {
  if (run(['pbkdf2', t[0], t[1], String(t[2]), '32']) !== crypto.pbkdf2Sync(t[0], t[1], t[2], 32, 'sha256').toString('hex')) bad++; });
ok('pbkdf2-sha256 vs node', !bad);
// 3. scrubbing: hand-picked lines and random ones from an alphabet full of the patterns' edges
var lines = [
  '[net] GET https://torrentio.strem.fun/realdebrid=ABCDEF1234567890abcdef1234567890/stream/movie/tt0111161.json?x=1#f',
  '[sync] signed in as some.one+tv@example.co.uk, authKey: "a1b2c3d4e5f6a7b8c9d0e1f2" token=abc, password = hunter22',
  '[p2p] magnet:?xt=urn:btih:0123456789abcdef0123456789ABCDEF01234567&dn=x 198.51.100.20:8080 10.0.0.1 1.2.3.4567',
  'https://user:pw@host.example/path/eyJhbGciOiJIUzI1NiJ9abc/ok/a=b/%20x|y?q=1',
  'https://a@/x https:///x http://h/ https://h?x=a@b.com/z "https://q.example/x"<b>',
  'Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.payload ACCESS_TOKEN : zzzz9999 api_key=:ab apikey==:abcd',
  'run of 24 aaaaaaaaaaaaaaaaaaaaaaaa and 23 bbbbbbbbbbbbbbbbbbbbbbb a1+b2-c3_d4e5f6g7h8i9j0k1l2== x-' + 'Z9'.repeat(15),
  'ctl\u0001chars\u001fhere\ttab\rcr  é…😀 https://ü.example/😀😀😀😀😀😀😀😀😀😀/x',
  'mail a@b@c.com x@y.z1 e.mail@sub.dom.ain.toolong123 foo@bar.c0m t@t.co.'
];
var ALPHA = ['a', 'Z', '0', '9', '1', '.', ':', '/', '?', '#', '=', '&', '@', '-', '_', '+', '%', '|', ' ', '"', "'", '<', ';', ',', '\t', ' ', '…', '😀', 'é', 'https://', 'http://', 'token', 'Bearer', 'secret', 'abcdef0123456789', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', '192.168.', '255.', '.com', 'x@', '\u0002'];
for (i = 0; i < 4000; i++) { var s = '', n = 1 + Math.floor(Math.random() * 40); for (var j = 0; j < n; j++) s += ALPHA[Math.floor(Math.random() * ALPHA.length)]; lines.push(s); }
lines.push('long ' + 'x'.repeat(1990) + 'yyyyyyyyyyyyyyy é'.repeat(3));
var tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'sblogs-')), inF = path.join(tmp, 'in'), outF = path.join(tmp, 'out');
fs.writeFileSync(inF, lines.join('\0') + '\0');
['line', 'share'].forEach(function (mode) {
  run(['scrub', mode, inF, outF]); var got = fs.readFileSync(outF, 'utf8').split('\0'), diff = 0, first = -1;
  for (var i = 0; i < lines.length; i++) { var want = mode === 'line' ? scrub(lines[i]) : lsScrub(lines[i]); if (got[i] !== want) { diff++; if (first < 0) first = i; } }
  ok('scrub ' + mode + ', ' + lines.length + ' lines', !diff, diff ? diff + ' differ, first:\n  in   ' + JSON.stringify(lines[first]) + '\n  got  ' + JSON.stringify(got[first]) + '\n  want ' + JSON.stringify(mode === 'line' ? scrub(lines[first]) : lsScrub(lines[first])) : '');
});
// 4. a sealed page opens with the web's own steps (a throwaway key pair here; the app seals to the developer's)
var dev = crypto.createECDH('prime256v1'); dev.generateKeys();
var other = crypto.createECDH('prime256v1'); other.generateKeys();
ok('p-256 shared secret vs node', run(['ecdh', other.getPrivateKey('hex'), dev.getPublicKey('hex')]) === other.computeSecret(dev.getPublicKey()).toString('hex'));
var code = 'K7M2', text = lsScrub(lines.slice(0, 9).join('\n')) + '\n' + 'filler line\n'.repeat(3000);
fs.writeFileSync(inF, text); run(['seal', code, dev.getPublicKey('base64'), inF, outF]);
var page = fs.readFileSync(outF, 'utf8'), opened = '';
try { opened = open(code, page, dev.getPrivateKey('base64')); } catch (e) { opened = 'ERROR ' + e.message; }
ok('sealed page opens (' + page.length + ' base64 chars)', opened === text);
var big = ''; for (i = 0; i < 30000; i++) big += crypto.randomBytes(6).toString('hex') + ' line ' + i + '\n'; /* hardly compresses: only the newest part fits */
fs.writeFileSync(inF, big); run(['seal', code, dev.getPublicKey('base64'), inF, outF]); page = fs.readFileSync(outF, 'utf8');
try { opened = open(code, page, dev.getPrivateKey('base64')); } catch (e) { opened = ''; }
var CUT = '[older lines left out: the page holds about 64 KB]\n';
ok('a large bundle keeps its newest part', opened.length > 1000 && opened.startsWith(CUT) && big.endsWith(opened.slice(CUT.length)) && page.length < 64000, page.length + ' chars, ' + opened.length + ' of ' + big.length + ' kept');
/* with the bundle's own header (version, build, device, then "== app =="): it stays, then the note, then the newest whole lines */
var head = 'Stremio Origin 1.1.0 build abc123\nwebOS 6.0 OLED55C1\nRAM 1.5 GB\n== app ==\n', bh = head + big;
fs.writeFileSync(inF, bh); run(['seal', code, dev.getPublicKey('base64'), inF, outF]); page = fs.readFileSync(outF, 'utf8');
try { opened = open(code, page, dev.getPrivateKey('base64')); } catch (e) { opened = ''; }
var rest = opened.slice(head.length + CUT.length);
ok('a large bundle keeps its header', opened.startsWith(head + CUT) && rest.length > 1000 && bh.endsWith(rest) && /^[0-9a-f]{12} line \d+\n/.test(rest) && page.length < 64000, page.length + ' chars, header kept, ' + rest.length + ' newest chars, whole lines');
var refused = false; try { open('K7M3', fs.readFileSync(outF, 'utf8'), dev.getPrivateKey('base64')); } catch (e) { refused = true; }
ok('another code does not open it', refused);
fs.rmSync(tmp, { recursive: true, force: true });
console.log(fails ? '\n' + fails + ' FAILED' : '\nall ok'); process.exit(fails ? 1 : 0);
