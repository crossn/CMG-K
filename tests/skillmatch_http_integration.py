"""Run with a compiled skillmatch_http_client_test executable path."""
import http.server
import subprocess
import socket
import sys
import threading
import time

exe = sys.argv[1]
records = []
responses = []

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        records.append((self.path, self.headers['Idempotency-Key'], body))
        action = responses.pop(0) if responses else 204
        if action == 'drop':
            self.close_connection = True
            return
        if action == 'timeout':
            time.sleep(1.3)
            return
        self.send_response(action)
        self.send_header('Content-Length', '0')
        self.end_headers()

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
port = str(server.server_address[1])
def run(mode): subprocess.run([exe, port, mode], check=True, timeout=12)
try:
    run('off'); run('direct'); assert not records
    responses[:] = [503, 'drop', 'timeout', 429, 204]
    run('retry')
    assert len(records) == 5 and len(set(records)) == 1, records
    assert records[0] == ('/observations', 'test-event', b'{"observation":true}')
    for status in (400, 409, 302, 200):
        records.clear(); responses[:] = [status]
        run('reject'); time.sleep(.15); assert len(records) == 1
    records.clear(); responses[:] = ['timeout']
    start = time.monotonic(); run('disable'); assert time.monotonic() - start < 2.5
    time.sleep(.4)
    records.clear(); responses[:] = []
    clients = [subprocess.Popen([exe, port, 'two', 'client-' + str(i)]) for i in (1, 2)]
    assert all(client.wait(timeout=12) == 0 for client in clients)
    assert {record[1] for record in records} == {'client-1', 'client-2'}
    records.clear()
    probe = socket.socket(); probe.bind(('127.0.0.1', 0)); late_port = probe.getsockname()[1]; probe.close()
    late_client = subprocess.Popen([exe, str(late_port), 'retry', 'reconnect'])
    time.sleep(.4)
    late_server = http.server.ThreadingHTTPServer(('127.0.0.1', late_port), Handler)
    threading.Thread(target=late_server.serve_forever, daemon=True).start()
    try:
        assert late_client.wait(timeout=12) == 0
        assert len(records) == 1 and records[0][1] == 'reconnect'
    finally:
        late_server.shutdown(); late_server.server_close()
    print('localhost HTTP retry/drop/offline recovery/timeout/429/ACK/reject/OFF/disable/two fake clients: PASS')
finally:
    server.shutdown(); server.server_close()
