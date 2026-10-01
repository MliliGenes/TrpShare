#!/usr/bin/env python3
"""End-to-end chunk protocol tests, including an actual file larger than 2 GiB."""
import argparse
import hashlib
import http.client
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.parse

CHUNK = 8 * 1024 * 1024
ROOT = Path(__file__).resolve().parents[1]


def run(binary, large):
    with tempfile.TemporaryDirectory(prefix='trpshare-test-') as folder:
        share = Path(folder)
        (share / 'nested').mkdir()
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        process = subprocess.Popen([str(binary), '--port', str(port), '--share', str(share)],
                                   cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        def request(method, path, body=None):
            connection = http.client.HTTPConnection('127.0.0.1', port, timeout=40)
            connection.request(method, path, body=body)
            response = connection.getresponse()
            status, data = response.status, response.read()
            connection.close()
            return status, data
        def api(method, path, body=None, expected=200):
            status, data = request(method, path, body)
            assert status == expected, (status, data, method, path)
            return json.loads(data)
        def begin(name, size, **extra):
            params = {'name': name, 'size': size, 'path': 'nested', **extra}
            return api('POST', '/api/uploads?' + urllib.parse.urlencode(params))
        def chunk(session, offset, body, expected=200):
            return api('PUT', '/api/uploads/' + session['id'] + '?offset=' + str(offset), body, expected)
        try:
            for _ in range(100):
                try:
                    request('GET', '/api/files')
                    break
                except OSError:
                    if process.poll() is not None: raise RuntimeError('server exited')
                    time.sleep(0.02)
            else: raise RuntimeError('server did not start')
            assert request('GET', '/upload.js')[0] == 200
            # Binary bytes, nested destination, partial final chunk and idempotent start.
            data = bytes(range(256)) * (CHUNK // 256 + 17)
            session = begin('binary file.bin', len(data), id='a' * 32)
            assert begin('binary file.bin', len(data), id='a' * 32)['id'] == session['id']
            api('POST', '/api/uploads/' + session['id'] + '/complete', expected=409)
            session = chunk(session, 0, data[:CHUNK])
            assert session['offset'] == CHUNK
            chunk(session, 0, data[:CHUNK], expected=409)  # response-loss retry
            assert api('GET', '/api/uploads/' + session['id'])['offset'] == CHUNK
            session = chunk(session, CHUNK, data[CHUNK:])
            result = api('POST', '/api/uploads/' + session['id'] + '/complete')
            assert result['completed']
            assert api('POST', '/api/uploads/' + session['id'] + '/complete')['completed']
            status, downloaded = request('GET', '/files/nested/binary%20file.bin')
            assert status == 200 and hashlib.sha256(downloaded).digest() == hashlib.sha256(data).digest()
            # A truncated connection must not advance the acknowledged offset.
            interrupted = begin('interrupted.bin', 4096)
            with socket.create_connection(('127.0.0.1', port), timeout=40) as sock:
                path = '/api/uploads/' + interrupted['id'] + '?offset=0'
                sock.sendall(('PUT ' + path + ' HTTP/1.1\r\nHost: localhost\r\nContent-Length: 4096\r\n\r\n').encode() + b'x' * 1024)
                sock.shutdown(socket.SHUT_WR)
                assert b'400' in sock.recv(4096)
            assert api('GET', '/api/uploads/' + interrupted['id'])['offset'] == 0
            chunk(interrupted, 0, b'y' * 4096)
            api('POST', '/api/uploads/' + interrupted['id'] + '/complete')
            assert (share / 'nested/interrupted.bin').read_bytes() == b'y' * 4096
            # Zero-byte files, cancellation, invalid lengths, and path restrictions.
            empty = begin('empty.txt', 0)
            api('POST', '/api/uploads/' + empty['id'] + '/complete')
            assert (share / 'nested/empty.txt').stat().st_size == 0
            cancelled = begin('cancelled.bin', 10)
            chunk(cancelled, 0, b'x' * 11, expected=400)
            api('DELETE', '/api/uploads/' + cancelled['id'])
            api('GET', '/api/uploads/' + cancelled['id'], expected=404)
            api('POST', '/api/uploads?name=escape&size=1&path=..', expected=400)
            api('POST', '/api/uploads?name=bad&size=-1', expected=400)
            api('POST', '/api/uploads?name=bad&size=18446744073709551616', expected=400)
            assert request('GET', '/files/%2e%2e/etc/passwd')[0] == 404
            print('PASS: binary assembly, nested download, retries, interruption, cancellation, empty file, invalid requests')
            if large:
                total = 2 * 1024 * 1024 * 1024 + 17
                session = begin('over-2GiB.bin', total)
                payload = b'Z' * CHUNK
                offset = 0
                while offset < total:
                    body = payload[:min(CHUNK, total - offset)]
                    session = chunk(session, offset, body)
                    offset = session['offset']
                api('POST', '/api/uploads/' + session['id'] + '/complete')
                path = share / 'nested/over-2GiB.bin'
                assert path.stat().st_size == total
                with path.open('rb') as file:
                    file.seek(2 * 1024 * 1024 * 1024 - 8)
                    assert file.read() == b'Z' * 25
                print('PASS: actual 2 GiB + 17 byte upload using 64-bit offsets')
            # Graceful shutdown cleans incomplete sessions.
            begin('unfinished.bin', 100)
        finally:
            process.terminate()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired: process.kill(); process.wait()
        assert not list(share.rglob('.trpshare-upload-*'))
        print('PASS: incomplete temporary files cleaned on shutdown')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=Path, default=ROOT / 'trpshare')
    parser.add_argument('--large', action='store_true', help='write and remove a 2 GiB test file')
    args = parser.parse_args()
    run(args.server.resolve(), args.large)
