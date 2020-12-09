using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace RpcClient
{
	enum ArgType : byte
	{
		Int = 0,
		Float,
		String,
		Blob,
		List,
	}

	enum RemoteError : byte
	{
		Ok,
		BadArgsCount,
		BadArgsType,
		BadMethod,
		UserError,
		InternalError,
		OtherError
	}

	class RemoteClient
	{
		public volatile TcpClient m_tcp;
		private volatile List<NetArg> m_calls;
		private RemoteClass m_class;

		public RemoteClient(TcpClient Client, RemoteClass Klass)
		{
			m_tcp = Client;
			m_calls = new List<NetArg>();
			m_class = Klass;
		}

		// - Returns true if the entire buffer was filled
		private bool ReadBytes(byte[] Buffer, int AlreadyRead)
		{
			NetworkStream stream = m_tcp.GetStream();
			int read = 0, pos = AlreadyRead;
			do
			{
				read = stream.Read(Buffer, pos, Buffer.Length - pos);
				pos += read;
			} while (read > 0 && pos < Buffer.Length);

			return pos == Buffer.Length;
		}

		/// <summary>
		/// - Blocking call to recieve and run the next command
		/// </summary>
		/// <returns>
		/// - Return == 0 on success, < 0 if stream data is invalid, or > 0 if the client made a bad call
		/// </returns>
		public int Recv()
		{
			try
			{
				NetworkStream stream = m_tcp.GetStream();
				byte[] buf = new byte[NetArg.HEADER_SIZE];

				if (!ReadBytes(buf, 0))
				{
					//Console.WriteLine("RemoteClient.Recv() couldn't receive arg header!");
					return -1;
				}

				if ((ArgType)buf[0] != ArgType.List)
				{
					Console.WriteLine($"RemoteClient.Recv() expected ArgType.List ({(int)ArgType.List}) but got {buf[0]}!");
					return -1;
				}

				int nextsize = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(buf, 1));
				if (nextsize <= 0)
				{
					Console.WriteLine($"RemoteClient.Recv() recieved invalid size {nextsize}!");
					return -1;
				}

				int pos = buf.Length, size = buf.Length + nextsize;
				Array.Resize(ref buf, size);

				if (!ReadBytes(buf, pos))
				{
					Console.WriteLine("RemoteClient.Recv() failed to recieve full arg!");
					return -1;
				}

				int used;
				NetArg arg = NetArg.UnpackArg(buf, 0, out used);
				if (arg == null)
				{
					Console.WriteLine("RemoteClient.Recv() failed to unpack arg!");
					return -1;
				}

				NetList args = arg as NetList;
				if (args.value.Count < 1 || args.value[0].type != ArgType.Int)
				{
					Console.WriteLine("RemoteClient.Recv() expected function hash!");
					return -1;
				}

				NetInt nethash = args.value[0] as NetInt;
				UInt64 hash = unchecked((UInt64)nethash.value);

				RemoteMethod meth = m_class.FindMethod(hash);
				if (meth == null)
				{
					Console.WriteLine($"RemoteClient.Recv() failed to find function hash {hash}!");
					return 1;
				}

				args.value.RemoveAt(0); // Lop off remote call header, leaving only method args
				return meth.Call(args) == RemoteError.Ok ? 0 : 1; // RemoteMethod handles errors from here
			}
			catch (Exception e) { Console.WriteLine(e); }
			return -1;
		}

		// - Blocking call to flush all pending commands
		public void Send()
		{
			try
			{
				NetworkStream stream = m_tcp.GetStream();
				foreach (var call in m_calls)
					stream.Write(call.PackArg());
			}
			catch (Exception e)
			{
				if (e is SocketException || e is System.IO.IOException)
					Console.WriteLine(e);
				else
					throw;
			}
		}

		public NetList RemoteCall(string Method, string FmtArgs, params object[] Args)
		{
			NetList list = new NetList();
			for (int i = 0; i < FmtArgs.Length; i++)
			{
				NetArg arg = NetArg.FromFmt(FmtArgs[i], Args[i]);
				if (arg == null)
				{
					Console.WriteLine($"Method '{Method}': Bad FmtArgs '{FmtArgs}' at index {i}");
					return null;
				}
				list.value.Add(arg);
			}

			list.value.Insert(0, new NetInt(unchecked((Int64)FNV1a.Hash(Method))));
			m_calls.Add(list);
			return list;
		}
	}

	class RemoteMethod
	{
		public string m_name { get; }
		public UInt64 m_hash { get; }
		public string m_fmt { get; }
		Func<NetList, bool> m_meth { get; }

		public RemoteMethod(string Name, string ArgsFormat, Func<NetList, bool> Method)
		{
			m_name = Name;
			m_hash = FNV1a.Hash(Name);
			m_fmt = ArgsFormat;
			m_meth = Method;
		}

		public RemoteError Call(NetList Args)
		{
			string fmt = Args.ToArgsFmt();
			if (fmt != m_fmt)
			{
				Console.WriteLine($"RemoteMethod '{m_name}': Invalid args. Expected '{m_fmt}' but got '{fmt}'");
				if (fmt.Length != m_fmt.Length)
					return RemoteError.BadArgsCount;
				return RemoteError.BadArgsType;
			}

			try
			{
				if (!m_meth(Args))
				{
					Console.WriteLine($"RemoteMethod '{m_name}': Bad user error");
					return RemoteError.UserError;
				}
			}
			catch (Exception e)
			{
				Console.WriteLine($"Exception in RemoteMethod '{m_name}':\n\t{e}");
				return RemoteError.InternalError;
			}

			return RemoteError.Ok;
		}
	}

	class RemoteClass
	{
		List<RemoteMethod> m_meths;

		public RemoteClass()
		{
			m_meths = new List<RemoteMethod>();
		}

		public void AddMethod(string Name, string ArgsFormat, Func<NetList, bool> Method)
		{
			m_meths.Add(new RemoteMethod(Name, ArgsFormat, Method));
		}
		public RemoteMethod FindMethod(UInt64 Hash)
		{
			return m_meths.Find(m => m.m_hash == Hash);
		}
		public RemoteMethod Find(string Str)
		{
			return FindMethod(FNV1a.Hash(Str));
		}
	}

	abstract class NetArg
	{
		public const int HEADER_SIZE = sizeof(byte) + sizeof(int);

		public ArgType type { get; }
		public abstract object obj { get; }

		protected NetArg(ArgType Type)
		{
			type = Type;
		}

		public abstract byte[] GetBytes();

		public byte[] PackArg()
		{
			byte[] bytes = GetBytes();
			byte[] all = new byte[sizeof(byte) + sizeof(int) + bytes.Length];
			all[0] = (byte)type;
			BitConverter.GetBytes(IPAddress.HostToNetworkOrder(bytes.Length)).CopyTo(all, sizeof(byte));
			bytes.CopyTo(all, sizeof(byte) + sizeof(int));
			return all;
		}

		public static NetArg UnpackArg(byte[] Bytes, int Offset, out int NumBytesUsed)
		{
			ArgType type = (ArgType)Bytes[Offset];
			int off = Offset + 1;

			int size = IPAddress.NetworkToHostOrder(BitConverter.ToInt32(Bytes, off));
			off += sizeof(int);

			NetArg result = null;

			if (size >= 0)
			{
				switch (type)
				{
					case ArgType.Int:
						result = new NetInt(Bytes, off); break;
					case ArgType.Float:
						result = new NetFloat(Bytes, off); break;
					case ArgType.Blob:
						result = new NetBlob(Bytes, off, size); break;
					case ArgType.String:
						result = new NetString(Bytes, off, size); break;
					case ArgType.List:
						result = new NetList(Bytes, off, size); break;
				}
			}

			off += size;

			NumBytesUsed = result != null ? off - Offset : 0;
			return result;
		}

		public char ToFmt(bool Except = false)
		{
			switch (type)
			{
				case ArgType.Int: return 'i';
				case ArgType.Float: return 'f';
				case ArgType.String: return 's';
				case ArgType.Blob: return 'b';
				case ArgType.List: return 'l';
			}
			if (Except)
				throw new NotImplementedException($"ToFmt() Does not support arg type: {type}! Maybe you forgot to add an Fmt case?");
			return (char)0;
		}

		public static NetArg FromFmt(char ArgType, object Arg, bool Except = true)
		{
			switch (ArgType)
			{
				case 'i': return new NetInt((int)Arg);
				case 'f': return new NetFloat((double)Arg);
				case 's': return new NetString((string)Arg);
				case 'b': return new NetBlob((byte[])Arg);
				case 'l': return new NetList((List<NetArg>)Arg);
			}
			if (Except)
				throw new NotImplementedException($"FromFmt() Does not support arg type: {ArgType}! Maybe you forgot to add an Fmt case?");
			return null;
		}
	}

	class NetList : NetArg
	{
		public List<NetArg> value;
		public override object obj => value;

		public NetList() : base(ArgType.List)
		{
			value = new List<NetArg>();
		}
		public NetList(List<NetArg> Args) : base(ArgType.List)
		{
			value = new List<NetArg>(Args);
		}

		public NetList(byte[] Bytes, int Offset, int Count) : base(ArgType.List)
		{
			value = new List<NetArg>();
			int off = Offset;

			do
			{
				int used;
				NetArg arg = NetArg.UnpackArg(Bytes, off, out used);
				if (arg == null || used == 0)
				{
					Console.WriteLine($"NetList ctor: Failed UnpackArg! used: {used}, arg: {arg}");
					value.Clear();
					break;
				}
				value.Add(arg);
				off += used;
			} while (off - Offset < Count);
		}

		public override byte[] GetBytes()
		{
			List<byte> all = new List<byte>();
			foreach (NetArg arg in value)
				all.AddRange(arg.PackArg());
			return all.ToArray();
		}

		public string ToArgsFmt()
		{
			string fmt = "";
			foreach (NetArg arg in value)
				fmt += arg.ToFmt();
			return fmt;
		}
	}

	class NetBlob : NetArg
	{
		public byte[] value;
		public override object obj => value;

		public NetBlob(byte[] Value, int Offset = 0, int Count = -1) : base(ArgType.Blob)
		{
			if (Count == -1)
				Count = Value.Length - Offset;

			value = new byte[Count];
			Array.Copy(Value, Offset, value, 0, Count);
		}

		public override byte[] GetBytes()
		{
			return value;
		}
	}

	class NetString : NetArg
	{
		public string value;
		public override object obj => value;

		public NetString(string Value) : base(ArgType.String)
		{
			value = Value;
		}
		public NetString(byte[] Bytes, int Offset, int Count) : base(ArgType.String)
		{
			value = Encoding.UTF8.GetString(Bytes, Offset, Count);
		}

		public override byte[] GetBytes()
		{
			return Encoding.UTF8.GetBytes(value);
		}
	}

	class NetFloat : NetArg
	{
		public double value;
		public override object obj => value;

		public NetFloat(double Value) : base(ArgType.Float)
		{
			value = Value;
		}
		public NetFloat(byte[] Bytes, int Offset) : base(ArgType.Float)
		{
			byte[] bytes = new byte[sizeof(double)];
			Array.Copy(Bytes, Offset, bytes, 0, bytes.Length);
			if (BitConverter.IsLittleEndian)
				Array.Reverse(bytes);

			value = BitConverter.ToDouble(bytes, 0);
		}

		public override byte[] GetBytes()
		{
			byte[] bytes = BitConverter.GetBytes(value);
			if (BitConverter.IsLittleEndian)
				Array.Reverse(bytes);

			return bytes;
		}
	}

	class NetInt : NetArg
	{
		public Int64 value;
		public override object obj => value;

		public NetInt(Int64 Value) : base(ArgType.Int)
		{
			value = Value;
		}
		public NetInt(byte[] Bytes, int Offset) : base(ArgType.Int)
		{
			value = IPAddress.NetworkToHostOrder(BitConverter.ToInt64(Bytes, Offset));
		}

		public override byte[] GetBytes()
		{
			return BitConverter.GetBytes(IPAddress.HostToNetworkOrder(value));
		}
	}

	static class FNV1a
	{
		// 64-bit FNV1-a: http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param

		const UInt64 default_offset_basis = 0xCBF29CE484222325;
		const UInt64 prime = 0x100000001B3;

		public static UInt64 Hash(string Str)
		{

			UInt64 hash = default_offset_basis;
			foreach (char c in Str)
			{
				hash ^= (UInt64)c;
				hash *= prime;
			}
			return hash;
		}
	}
}
