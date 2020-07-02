import sys
import os
import socket
import struct
import random
import threading
import time
import base64
import zlib

import port_proxy

def recvExact(sock, l):
    bufs = []
    while l > 0:
        try:
            buf = sock.recv(l)
            if not buf:
                break
            l -= len(buf)
            bufs.append(buf)
        except socket.timeout:
            pass
    return ''.join(bufs)

def startSolver(initialIncumbent, fork=True):
    solver = sys.argv[1]
    stub = sys.argv[2]
    paramsFile = sys.argv[4]
    args = [solver, stub, '-p']
    if initialIncumbent < 1e22:
        args.append('-b')
        args.append('%g' % initialIncumbent)

    with open(paramsFile, 'r') as f:
        otherArgs = f.read().split('\n')
    if otherArgs:
        args.append('--')
        args.extend(otherArgs)

    return port_proxy.startSolver(args, fork)

class Task:

    def __init__(self):
        port = int(os.environ.get('EVEREST_AGENT_PORT', 0))
        address = os.environ.get('EVEREST_AGENT_ADDRESS', 'localhost')
        task_id = os.environ.get('EVEREST_AGENT_TASK_ID')

        sys.stderr.write(">>> hostname: %s\n" % socket.gethostname())

        # connect to agent
        self.sock = None
        if port:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((address, port))
            print 'Connected to agent, setting message mode'
            self.sock.sendall(struct.pack('>Ib', len(task_id) + 1, 0))
            self.sock.sendall(task_id)
            self.sock.settimeout(0.5)
        self.running = True

    def requestVar(self, name):
        msg = "VAR_GET %s" % name
        self.send_message(msg)

    def requestAndWaitVars(self):
        if not self.sock:
            return {
                self.stoppedVar : 'NULL',
                'record' : '1e30'
            }
        self.requestVar(self.stoppedVar)
        self.requestVar('record')
        values = {}
        while True:
            try:
                resp = self.receive_message()
            except socket.timeout:
                continue
            assert resp, 'Server connection closed'
            if not resp.split()[1] in [self.stoppedVar, 'record']:
                continue
            values[resp.split()[1]] = resp.split()[2]
            if len(values) == 2:
                break
        return values

    def run(self):
        stub = sys.argv[2]
        self.stopMode = int(sys.argv[3])
        initialIncumbent = float(sys.argv[5])
        # time.sleep(random.uniform(1, 10))

        self.stoppedVar = os.path.splitext(stub)[0] + '_stopped'

        # get current record and stop state
        vals = self.requestAndWaitVars()

        stopped = vals[self.stoppedVar] != 'NULL'
        if self.stopMode and stopped:
            self.sock.shutdown(socket.SHUT_WR)
            os.mknod(os.path.splitext(stub)[0] + '.sol')
            return

        cur_record = vals['record']
        if cur_record != "NULL":
            initialIncumbent = min(initialIncumbent, float(cur_record))

        self.solver = startSolver(initialIncumbent)

        if self.sock:
            # receive record updates in a separate thread
            receiver = threading.Thread(target=self.receive_records)
            receiver.start()

        hadSmth = False
        while self.running:
            solverMsg = port_proxy.readFromSolver(self.solver)
            if solverMsg[0] in ['incumbent', 'result', 'incumbent-seq']:
                hadSmth = True
                print "Found new record: %f" % solverMsg[1]
                if solverMsg[0] == 'incumbent-seq':
                    seqNumber = solverMsg[2]
                    with open('outsol-%d.sol' % seqNumber, 'rb') as f:
                        solution = f.read()
                    with open(os.path.splitext(stub)[0] + '.sol', 'wb') as f:
                        f.write(solution)
                    print "Found new record: %f seq: %d size: %d" % (
                        solverMsg[1], solverMsg[2], len(solution))
                    msg = "VAR_SET_MD record %f:%s" % (
                        solverMsg[1], base64.standard_b64encode(zlib.compress(solution)))
                else:
                    print "Found new record: %f" % solverMsg[1]
                    msg = "VAR_SET_MD record %f" % solverMsg[1]
                self.send_message(msg)
                if solverMsg[0] == 'result':
                    with open(os.path.splitext(stub)[0] + '.sol', 'r') as f:
                        firstLine = f.readline()
                    sys.stderr.write(">>> solutionHeader: %s\n" % firstLine)
                if self.stopMode and solverMsg[0] == 'result':
                    print 'Got result, stopping other solvers...'
                    self.send_message('VAR_SET_MD %s 1' % self.stoppedVar)
            elif solverMsg[0] == 'closed':
                if not hadSmth:
                    print 'Warning: No data from solver received'
                self.running = False
                if self.sock:
                    self.sock.shutdown(socket.SHUT_WR)
                    receiver.join()
                sys.stderr.write(">>> solver_exitcode: %s\n" % solverMsg[1])
                print 'Finished', solverMsg
                return solverMsg[1]
        return 0

    def send_message(self, msg):
        if self.sock:
            self.sock.sendall(struct.pack('>I', len(msg)))
            self.sock.sendall(msg)

    def receive_message(self):
        header = recvExact(self.sock, 4)
        if not header:
            return ''
        size, = struct.unpack('>I', header)
        msg = recvExact(self.sock, size)
        print "%s Received message: %s..." % (time.ctime(), msg[:25])
        return msg

    def receive_records(self):
        killing = False
        nextKill = None
        killDelay = 1
        seqNumber = 0
        while self.running:
            try:
                msg = self.receive_message()
                if not msg:
                    self.running = False
                    break
                if msg.startswith('VAR_VALUE record'):
                    value = msg.split()[2]
                    if not ':' in value:
                        record = float(msg.split()[2])
                        port_proxy.sendIncumbent(self.solver, record)
                        print "Updated record: %f" % record
                    else:
                        spl = value.split(':')
                        record = float(spl[0])
                        solution = zlib.decompress(base64.standard_b64decode(spl[1]))
                        seqNumber += 1
                        with open('insol-%d.sol' % seqNumber, 'wb') as f:
                            f.write(solution)
                        port_proxy.sendIncumbent(self.solver, record, seqNumber)
                        print "Updated record: %f, seq %d, size %d, comp-size %d" % (
                            record, seqNumber, len(solution), len(spl[1]))
                elif self.stopMode and msg.startswith('VAR_VALUE %s' % self.stoppedVar):
                    assert(msg.split()[2] == '1')
                    killing = True
                    nextKill = time.time() + killDelay
                    print "Got stop message. Stopping solver..."
                    port_proxy.stopSolver(self.solver)
                    print "Sent SIGINT to solver"
                elif self.stopMode and msg.startswith('VAR_VALUE') and 'stopped' in msg:
                    print 'Got stopped message for other stub:', msg
                else:
                    assert False, 'Unknown message: %s' % msg
            except socket.timeout:
                if killing and time.time() >= nextKill:
                    nextKill = time.time() + killDelay
                    port_proxy.stopSolver(self.solver)
                    print "Sent SIGINT to solver"
            except:
                self.running = False
                raise

    def shutdown(self):
        self.running = False
        if self.sock:
            self.sock.close()

def main():
    fork = os.environ.get("OMPI_COMM_WORLD_RANK", "0") == "0"
    if not fork:
        startSolver(float(sys.argv[5]), fork)
    task = Task()
    try:
        return task.run()
    finally:
        task.shutdown()


if __name__ == "__main__":
    main()
