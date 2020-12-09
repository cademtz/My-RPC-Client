from rpcclient import *
import socket

RPCHOST = '127.0.0.1'
RPCPORT = 11223

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((RPCHOST, RPCPORT))

klass = RemoteClass()
def wazzap(args):
    s = args.val[0].val
    f = args.val[1].val
    print('wazzap(' + s + ', ' + str(f) + ')')
klass.AddMethod('wazzap', 'sf', wazzap)
client = RpcClient(klass)

args = NetList()
args.AddItem(NetInt(80085))
args.AddItem(NetString('pogree\ts..'))
args.AddItem(NetFloat(4.200026699320269))
client.RemoteCall('Say_IntString', args)
client.Send(s)
while client.Recv(s, asyncc=True) == None: pass
client.RemoteCall('CloseServer')
client.Send(s)
while client.Recv(s, asyncc=True) == None: pass

s.close()
print('Finished running client')

import time
time.sleep(5)