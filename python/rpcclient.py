import struct
import sys
import socket

# I'm no python programmer, and this was whipped up overnight. It works nicely though!

NetArgType_Int =    0
NetArgType_Float =  1
NetArgType_String = 2
NetArgType_Blob =   3
NetArgType_List =   4
NETHEADER = '!Bi'
NETHEADER_SIZE = struct.calcsize(NETHEADER)
NETHEADER_TYPE = 0
NETHEADER_LEN = 1
NETHEADER_VAL = 2

PY_LONG = int
if sys.version_info.major < 3:
    PY_LONG = long

FNV1A_OFFSET = 0xCBF29CE484222325
FNV1A_PRIME = 0x100000001B3

class RpcClient():
    
    def __init__(self, remoteClass):
        self.klass = remoteClass
        self.calls = []
        self._pending = bytes(b'')
        self._hedr = None
    
    @staticmethod
    def HashString(somestr):
        hash = FNV1A_OFFSET
        for c in somestr:
            hash ^= ord(c)
            hash *= FNV1A_PRIME
            hash &= 0xffffffffffffffff
        return hash
    
    def RemoteCall(self, Name, netList = None):
        callList = NetList()
        callList.AddItem(NetInt(RpcClient.HashString(Name)))

        if netList != None:
            for arg in netList.val:
                callList.AddItem(arg)
        self.calls.append(callList)
    
    def Send(self, sock):
        for call in self.calls:
            sock.sendall(call.PackArg())
        self.calls = []
    
    def _ReadBytes(self, sock, Count):
        self._pending = bytes(b'')

        buf = sock.recv(Count)
        while len(buf) < Count:
            moar = sock.recv(Count - len(buf))
            if not moar:
                return None
            buf += moar
        return buf
    
    def _TryReadBytes(self, sock, Count):
        self._pending += sock.recv(Count - len(self._pending))
        if len(self._pending) < Count:
            return None
        buf = self._pending
        self._pending = bytes(b'')
        return buf
    
    @staticmethod
    def PeekHeader(sock):
        buf = sock.recv(NETHEADER_SIZE, socket.MSG_PEEK)
        if not buf or len(buf) != NETHEADER_SIZE:
            return None
        return NetArg.GetHeader(buf)

    def Recv(self, sock, asyncc=False):
        hedr = self._hedr
        if hedr == None:
            if asyncc: hedr = self._hedr = RpcClient.PeekHeader(sock)
            else: hedr = NetArg.GetHeader(self._ReadBytes(sock, NETHEADER_SIZE))
        
        if hedr == None:
            if asyncc:
                return None
            print("RemoteClient.Recv() couldn't receive arg header!")
            return -1
        
        self._hedr = hedr
        
        if hedr[NETHEADER_TYPE] != NetArgType_List:
            print('RemoteClient.Recv() expected ArgType.List ({}) but got {}!'
                .format(NetArgType_List, hedr[NETHEADER_TYPE]))
            return -1
        
        nextsize = hedr[NETHEADER_LEN]
        if nextsize <= 0:
            print('RemoteClient.Recv() recieved invalid size {}!'
                .format(nextsize))
            return -1
        
        read = self._TryReadBytes if asyncc else self._ReadBytes
        if asyncc:
            nextsize += NETHEADER_SIZE
        buf = read(sock, nextsize)
        if not buf:
            if asyncc:
                return None
            print('RemoteClient.Recv() failed to recieve full arg!')
            return -1

        self._hedr = None

        args = NetArg.UnpackArg(buf)
        if not args:
            print('RemoteClient.Recv() failed to unpack arg!')
            return -1
        
        if len(args.val) < 1 or args.val[0].nettype != NetArgType_Int:
            print('RemoteClient.Recv() expected function hash!')
            return -1
        
        nethash = args.val[0].val
        meth = self.klass.FindMethod(nethash)
        if not meth:
            print('RemoteClient.Recv() failed to find function hash {}!'
                .format(nethash))
            return 1
        
        args.val.pop(0)
        meth.Call(args)

class RemoteClass():
    def __init__(self):
        self.methods = []
    
    def AddMethod(self, Name, ArgsFormat, Method):
        self.methods.append(RemoteMethod(Name, ArgsFormat, Method))
    
    def FindMethod(self, Hash):
        for meth in self.methods:
            if meth.hash == Hash:
                return meth
        return None

class RemoteMethod():

    def __init__(self, Name, ArgsFormat, Method):
        self.name = Name
        self.fmt = ArgsFormat
        self.method = Method
        self.hash = RpcClient.HashString(Name)
    
    def Call(self, netlist):
        return self.method(netlist)

class NetArg():

    def GetBytes(self):
        pass

    def PackArg(self):
        bites = self.GetBytes()
        return struct.pack(NETHEADER, self.nettype, len(bites)) + bites

    @staticmethod
    def GetHeader(Bytes, Off = 0):
        if len(Bytes) - Off < NETHEADER_SIZE:
            return None
        return struct.unpack(NETHEADER, Bytes[Off:Off + NETHEADER_SIZE])
    @staticmethod
    def GetPackedSize(hedr):
        return NETHEADER_SIZE + hedr[NETHEADER_LEN]

    @staticmethod
    def UnpackArg(Bytes, Off = 0):
        hedr = NetArg.GetHeader(Bytes, Off)
        typ = hedr[NETHEADER_TYPE]
        llen = hedr[NETHEADER_LEN]

        start = Off + NETHEADER_SIZE
        bitez = Bytes[start:start + llen]
        if typ == NetArgType_Int: return NetInt(bitez)
        if typ == NetArgType_Float: return NetFloat(bitez)
        if typ == NetArgType_String: return NetString(bitez)
        if typ == NetArgType_Blob: return NetBlob(bitez)
        if typ == NetArgType_List: return NetList(bitez)
    

class NetInt(NetArg):

    def __init__(self, intOrBytes):
        self.nettype = NetArgType_Int
        if isinstance(intOrBytes, (int, PY_LONG)):
            self.val = intOrBytes
        elif isinstance(intOrBytes, bytes):
            try: self.val = struct.unpack('!q', intOrBytes)[0]
            except: self.val = struct.unpack('!Q', intOrBytes)[0]
    
    def GetBytes(self):
        result = None
        try: result = struct.pack('!q', self.val)
        except: result = struct.pack('!Q', self.val)
        return result

class NetFloat(NetArg):

    def __init__(self, flOrBytes):
        self.nettype = NetArgType_Float
        if isinstance(flOrBytes, float):
            self.val = flOrBytes
        elif isinstance(flOrBytes, bytes):
            self.val = struct.unpack('!d', flOrBytes)[0]
    
    def GetBytes(self):
        return struct.pack('!d', self.val)

class NetString(NetArg):

    def __init__(self, strOrBytes):
        self.nettype = NetArgType_String
        if isinstance(strOrBytes, str):
            self.val = strOrBytes
        elif isinstance(strOrBytes, bytes):
            self.val = str(strOrBytes)
    
    def GetBytes(self):
        return self.val.encode('utf8')

class NetBlob(NetArg):

    def __init__(self, bytez):
        self.nettype = NetArgType_Blob
        self.val = bytez
    
    def GetBytes(self):
        return self.val

class NetList(NetArg):

    def __init__(self, fromBytes = None):
        self.nettype = NetArgType_List
        self.val = []

        if fromBytes != None:
            off = 0
            while off < len(fromBytes):
                nexth = NetArg.GetHeader(fromBytes, off)
                llen = NetArg.GetPackedSize(nexth)
                if off + llen > len(fromBytes):
                    break
                self.AddItem(NetArg.UnpackArg(fromBytes, off))
                off += llen
    
    def AddItem(self, netitem):
        if netitem != None:
            self.val.append(netitem)
    
    def GetBytes(self):
        bytez = bytes(b'')
        for arg in self.val:
            bytez += arg.PackArg()
        return bytez